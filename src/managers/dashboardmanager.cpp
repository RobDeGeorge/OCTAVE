// dashboardmanager.cpp

#include "dashboardmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTemporaryFile>

Q_LOGGING_CATEGORY(lcDashboards, "octave.dashboards")

DashboardManager::DashboardManager(QObject *parent)
    : QObject(parent)
{
    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { m_rescanDebounce.start(); });
    connect(&m_rescanDebounce, &QTimer::timeout, this, &DashboardManager::onUserDirChanged);
}

void DashboardManager::setPresetsDir(const QString &absolutePath)
{
    m_presetsDir = absolutePath;
}

void DashboardManager::setUserDir(const QString &absolutePath)
{
    m_userDir = absolutePath;
    rescanAll();
    watchUserDir();
}

void DashboardManager::watchUserDir()
{
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (!m_userDir.isEmpty() && QDir(m_userDir).exists())
        m_watcher.addPath(m_userDir);
}

QString DashboardManager::userDirSignatureNow() const
{
    if (m_userDir.isEmpty()) return {};
    QDir u(m_userDir);
    if (!u.exists()) return {};
    QStringList parts;
    const auto files = u.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        parts << QStringLiteral("%1:%2:%3")
                     .arg(fi.fileName())
                     .arg(fi.lastModified().toSecsSinceEpoch())
                     .arg(fi.size());
    }
    return parts.join(QLatin1Char('|'));
}

void DashboardManager::onUserDirChanged()
{
    const QString sig = userDirSignatureNow();
    if (sig == m_userDirSignature)
        return;
    qCInfo(lcDashboards) << "Dashboard user dir changed on disk -- rescanning";
    rescanAll();
    // A recreated directory drops the watch; re-arm.
    if (!m_watcher.directories().contains(m_userDir))
        watchUserDir();
}

void DashboardManager::refresh()
{
    rescanAll();
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

QVariantMap DashboardManager::findEntry(const QString &id) const
{
    for (const QVariant &v : m_dashboards) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m;
    }
    return {};
}

QSet<QString> DashboardManager::existingIds() const
{
    QSet<QString> ids;
    for (const QVariant &v : m_dashboards)
        ids.insert(v.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

bool DashboardManager::isBuiltIn(const QString &id) const
{
    return findEntry(id).value(QStringLiteral("builtIn")).toBool();
}

QVariant DashboardManager::loadDashboard(const QString &id)
{
    QVariantMap entry = findEntry(id);
    if (entry.isEmpty()) {
        qCWarning(lcDashboards) << "loadDashboard: unknown id" << id;
        return QVariantMap{};
    }
    return readFullSpec(entry.value(QStringLiteral("path")).toString());
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void DashboardManager::ensureUserDir() const
{
    if (m_userDir.isEmpty()) return;
    QDir().mkpath(m_userDir);
}

void DashboardManager::rescanAll()
{
    m_dashboards.clear();
    QSet<QString> takenIds;

    // Built-in presets first — these are authoritative; any user JSON that
    // tries to reuse a built-in id gets rejected below.
    if (!m_presetsDir.isEmpty()) {
        QDir d(m_presetsDir);
        if (d.exists()) {
            const auto files = d.entryInfoList(
                {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
            for (const QFileInfo &fi : files) {
                QVariantMap header = readHeader(fi.absoluteFilePath());
                if (header.isEmpty()) continue;
                QString id = header.value(QStringLiteral("id")).toString();
                if (id.isEmpty() || takenIds.contains(id)) {
                    qCWarning(lcDashboards)
                        << "Skipping preset with missing or duplicate id:"
                        << fi.fileName();
                    continue;
                }
                header.insert(QStringLiteral("builtIn"), true);
                header.insert(QStringLiteral("path"), fi.absoluteFilePath());
                m_dashboards.append(header);
                takenIds.insert(id);
            }
        } else {
            qCWarning(lcDashboards) << "Presets dir does not exist:" << m_presetsDir;
        }
    }

    // User dashboards.
    ensureUserDir();
    if (!m_userDir.isEmpty()) {
        QDir u(m_userDir);
        if (u.exists()) {
            const auto files = u.entryInfoList(
                {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
            for (const QFileInfo &fi : files) {
                QVariantMap header = readHeader(fi.absoluteFilePath());
                if (header.isEmpty()) continue;
                QString id = header.value(QStringLiteral("id")).toString();
                if (id.isEmpty()) {
                    qCWarning(lcDashboards)
                        << "Skipping user dashboard with missing id:"
                        << fi.fileName();
                    continue;
                }
                if (takenIds.contains(id)) {
                    qCWarning(lcDashboards)
                        << "Skipping user dashboard — id collides with built-in:"
                        << id << "(" << fi.fileName() << ")";
                    continue;
                }
                header.insert(QStringLiteral("builtIn"), false);
                header.insert(QStringLiteral("path"), fi.absoluteFilePath());
                m_dashboards.append(header);
                takenIds.insert(id);
            }
        }
    }

    qCInfo(lcDashboards) << "Scanned dashboards:"
                         << m_dashboards.size() << "total";
    m_userDirSignature = userDirSignatureNow();
    emit dashboardsChanged();
}

// ---------------------------------------------------------------------------
// JSON I/O
// ---------------------------------------------------------------------------

QVariantMap DashboardManager::readHeader(const QString &absolutePath) const
{
    QVariantMap full = readFullSpec(absolutePath);
    if (full.isEmpty()) return {};

    QVariantMap header;
    header.insert(QStringLiteral("id"),    full.value(QStringLiteral("id")));
    header.insert(QStringLiteral("label"), full.value(QStringLiteral("label")));
    return header;
}

QVariantMap DashboardManager::readFullSpec(const QString &absolutePath) const
{
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcDashboards) << "Cannot open dashboard file:" << absolutePath;
        return {};
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcDashboards) << "Invalid JSON in" << absolutePath
                                << ":" << err.errorString();
        return {};
    }
    if (!doc.isObject()) {
        qCWarning(lcDashboards) << "Dashboard JSON root must be an object:"
                                << absolutePath;
        return {};
    }
    return doc.object().toVariantMap();
}

bool DashboardManager::writeSpec(const QString &absolutePath, const QVariantMap &spec)
{
    QJsonDocument doc(QJsonObject::fromVariantMap(spec));

    QFileInfo fi(absolutePath);
    QDir().mkpath(fi.absolutePath());

    // Atomic write: temp file + rename.
    QTemporaryFile tmp(fi.absolutePath() + QStringLiteral("/.dashboard_tmp_XXXXXX.json"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        qCWarning(lcDashboards) << "Cannot create temp file:" << tmp.errorString();
        return false;
    }
    tmp.write(doc.toJson(QJsonDocument::Indented));
    const QString tmpName = tmp.fileName();
    tmp.close();

#ifdef Q_OS_WIN
    if (QFile::exists(absolutePath)) QFile::remove(absolutePath);
#endif
    if (!QFile::rename(tmpName, absolutePath)) {
        qCWarning(lcDashboards) << "Atomic rename failed, falling back to direct write";
        QFile::remove(tmpName);
        QFile direct(absolutePath);
        if (!direct.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qCWarning(lcDashboards) << "Cannot open" << absolutePath << "for writing";
            return false;
        }
        direct.write(doc.toJson(QJsonDocument::Indented));
        direct.close();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

QString DashboardManager::saveDashboard(const QVariantMap &spec)
{
    const QString id    = spec.value(QStringLiteral("id")).toString();
    const QString label = spec.value(QStringLiteral("label")).toString();

    if (id.isEmpty() || label.isEmpty()) {
        qCWarning(lcDashboards) << "saveDashboard requires id and label";
        return {};
    }
    if (m_userDir.isEmpty()) {
        qCWarning(lcDashboards) << "User dashboards dir not configured";
        return {};
    }

    // Don't allow saving over a built-in id.
    QVariantMap existing = findEntry(id);
    if (!existing.isEmpty() && existing.value(QStringLiteral("builtIn")).toBool()) {
        qCWarning(lcDashboards)
            << "Cannot save: id" << id << "collides with a built-in preset";
        return {};
    }

    const QString path = m_userDir + QDir::separator() + id + QStringLiteral(".json");
    if (!writeSpec(path, spec)) return {};

    rescanAll();
    return id;
}

bool DashboardManager::deleteDashboard(const QString &id)
{
    QVariantMap entry = findEntry(id);
    if (entry.isEmpty()) return false;
    if (entry.value(QStringLiteral("builtIn")).toBool()) {
        qCWarning(lcDashboards) << "Refusing to delete built-in dashboard:" << id;
        return false;
    }

    const QString path = entry.value(QStringLiteral("path")).toString();
    if (!QFile::remove(path)) {
        qCWarning(lcDashboards) << "Failed to delete:" << path;
        return false;
    }

    rescanAll();
    return true;
}

QString DashboardManager::duplicateDashboard(const QString &sourceId,
                                             const QString &newLabel)
{
    QVariantMap entry = findEntry(sourceId);
    if (entry.isEmpty()) {
        qCWarning(lcDashboards) << "duplicateDashboard: unknown source id" << sourceId;
        return {};
    }

    QVariantMap spec = readFullSpec(entry.value(QStringLiteral("path")).toString());
    if (spec.isEmpty()) return {};

    const QString finalLabel = newLabel.isEmpty()
        ? (entry.value(QStringLiteral("label")).toString() + QStringLiteral(" (Copy)"))
        : newLabel;
    const QString newId = uniqueUserIdFromLabel(finalLabel);

    spec.insert(QStringLiteral("id"),    newId);
    spec.insert(QStringLiteral("label"), finalLabel);

    return saveDashboard(spec);
}

// ---------------------------------------------------------------------------
// ID generation
// ---------------------------------------------------------------------------

QString DashboardManager::slugify(const QString &label)
{
    QString s = label.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-'))   s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("dashboard");
    return s;
}

QString DashboardManager::uniqueUserIdFromLabel(const QString &label) const
{
    const QString base = slugify(label);
    const QSet<QString> taken = existingIds();

    if (!taken.contains(base)) return base;

    for (int i = 2; i < 1000; ++i) {
        const QString candidate = base + QStringLiteral("-") + QString::number(i);
        if (!taken.contains(candidate)) return candidate;
    }
    // Astronomically unlikely; last resort.
    return base + QStringLiteral("-") + QString::number(QDateTime::currentMSecsSinceEpoch());
}
