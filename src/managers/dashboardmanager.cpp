// dashboardmanager.cpp

#include "dashboardmanager.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

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

    // Atomic write via QSaveFile (temp file, fsync, rename over the target) so
    // a power cut leaves either the old dashboard or the new one. QFile::rename
    // refuses to overwrite, so the old temp+rename always fell back to a
    // non-atomic direct write when saving over an existing dashboard.
    QSaveFile out(absolutePath);
    if (!out.open(QIODevice::WriteOnly)) {
        qCWarning(lcDashboards) << "Cannot open" << absolutePath << "for writing:"
                                << out.errorString();
        return false;
    }
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    if (out.write(data) != data.size() || !out.commit()) {
        qCWarning(lcDashboards) << "Failed to write" << absolutePath << ":"
                                << out.errorString();
        return false;
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

// ---------------------------------------------------------------------------
// Sharing (export / import)
// ---------------------------------------------------------------------------

namespace {
// Shared dashboards are small (a busy one is a few KB); the caps only stop a
// wrong or hostile file from tying up the UI thread.
constexpr qint64 kMaxImportBytes = 512 * 1024;
constexpr int kMaxCells = 256;
constexpr int kMaxGrid = 48;
constexpr int kMaxLabelLength = 80;
constexpr int kMaxPropString = 200;
}

void DashboardManager::setShareError(const QString &message)
{
    if (m_lastShareError == message) return;
    m_lastShareError = message;
    emit lastShareErrorChanged();
}

QString DashboardManager::exportDashboard(const QString &id)
{
    const QVariantMap spec = loadDashboard(id).toMap();
    if (spec.isEmpty()) {
        setShareError(QStringLiteral("Dashboard not found"));
        return {};
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = base + QStringLiteral("/OCTAVE-dashboards/") + id + QStringLiteral(".json");
    if (!writeSpec(path, spec)) {
        setShareError(QStringLiteral("Could not write to %1").arg(QFileInfo(path).absolutePath()));
        return {};
    }
    qCInfo(lcDashboards) << "Exported dashboard" << id << "to" << path;
    setShareError({});
    return path;
}

bool DashboardManager::copyDashboardToClipboard(const QString &id)
{
    const QVariantMap spec = loadDashboard(id).toMap();
    QClipboard *cb = QGuiApplication::clipboard();
    if (spec.isEmpty() || !cb) {
        setShareError(spec.isEmpty() ? QStringLiteral("Dashboard not found")
                                     : QStringLiteral("Clipboard unavailable"));
        return false;
    }
    cb->setText(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(spec))
                                      .toJson(QJsonDocument::Compact)));
    setShareError({});
    return true;
}

QString DashboardManager::importDashboard(const QString &fileOrUrl)
{
    // file:// → local path; content:// (Android picker) and plain paths are
    // handed to QFile as-is (Qt's Android file engine opens content URIs).
    const QUrl url(fileOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setShareError(QStringLiteral("Could not open the file"));
        return {};
    }
    if (f.size() > kMaxImportBytes) {
        setShareError(QStringLiteral("File is too large to be a dashboard"));
        return {};
    }
    return importDashboardFromText(QString::fromUtf8(f.read(kMaxImportBytes + 1)));
}

QString DashboardManager::importDashboardFromClipboard()
{
    QClipboard *cb = QGuiApplication::clipboard();
    const QString text = cb ? cb->text() : QString();
    if (text.trimmed().isEmpty()) {
        setShareError(QStringLiteral("The clipboard is empty"));
        return {};
    }
    return importDashboardFromText(text);
}

QString DashboardManager::importDashboardFromText(const QString &json)
{
    const QByteArray data = json.trimmed().toUtf8();
    if (data.size() > kMaxImportBytes) {
        setShareError(QStringLiteral("Text is too large to be a dashboard"));
        return {};
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        setShareError(QStringLiteral("Not an OCTAVE dashboard (invalid JSON)"));
        return {};
    }
    return importSpec(doc.object().toVariantMap());
}

QString DashboardManager::importSpec(const QVariantMap &raw)
{
    QString error;
    QVariantMap spec = sanitizeImportedSpec(raw, &error);
    if (spec.isEmpty()) {
        setShareError(error);
        return {};
    }
    const QString id = uniqueUserIdFromLabel(spec.value(QStringLiteral("label")).toString());
    spec.insert(QStringLiteral("id"), id);
    const QString saved = saveDashboard(spec);
    if (saved.isEmpty()) {
        setShareError(QStringLiteral("Could not save the imported dashboard"));
        return {};
    }
    qCInfo(lcDashboards) << "Imported dashboard as" << saved;
    setShareError({});
    return saved;
}

QVariantMap DashboardManager::sanitizeImportedSpec(const QVariantMap &raw, QString *error)
{
    auto fail = [error](const QString &msg) { if (error) *error = msg; return QVariantMap{}; };

    // JSON numbers only (no bools, no numeric strings) — same rule as the
    // Python _int_in().
    auto intIn = [](const QVariant &v, int lo, int hi, int def) {
        switch (v.typeId()) {
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::Double:
            return qBound(lo, int(qBound(-1e9, v.toDouble(), 1e9)), hi);
        default:
            return def;
        }
    };

    const int schema = intIn(raw.value(QStringLiteral("schema"), 1), -1, 1000, 0);
    if (schema > kSupportedSchema)
        return fail(QStringLiteral("This dashboard needs a newer version of OCTAVE"));
    if (schema < 1)
        return fail(QStringLiteral("Not an OCTAVE dashboard (bad schema)"));

    const QVariant cellsVar = raw.value(QStringLiteral("cells"));
    if (cellsVar.typeId() != QMetaType::QVariantList)
        return fail(QStringLiteral("Not an OCTAVE dashboard (no cells)"));
    const QVariantList rawCells = cellsVar.toList();
    if (rawCells.size() > kMaxCells)
        return fail(QStringLiteral("Dashboard has too many widgets"));

    QVariantMap out;
    out.insert(QStringLiteral("schema"), kSupportedSchema);

    QString label = raw.value(QStringLiteral("label")).toString().simplified().left(kMaxLabelLength);
    if (label.isEmpty()) label = QStringLiteral("Imported dashboard");
    out.insert(QStringLiteral("label"), label);

    const int cols = intIn(raw.value(QStringLiteral("gridColumns")), 1, kMaxGrid, 12);
    const int rows = intIn(raw.value(QStringLiteral("gridRows")), 1, kMaxGrid, 6);
    out.insert(QStringLiteral("gridColumns"), cols);
    out.insert(QStringLiteral("gridRows"), rows);
    for (const QString key : {QStringLiteral("margins"), QStringLiteral("spacing")}) {
        if (raw.contains(key))
            out.insert(key, intIn(raw.value(key), 0, 200, 0));
    }

    QVariantList cells;
    for (const QVariant &cv : rawCells) {
        if (cv.typeId() != QMetaType::QVariantMap) continue;
        const QVariantMap c = cv.toMap();
        const QString type = c.value(QStringLiteral("type")).toString();
        if (type.isEmpty() || type.size() > 64) continue;

        QVariantMap cell;
        cell.insert(QStringLiteral("type"), type);
        cell.insert(QStringLiteral("paramId"), c.value(QStringLiteral("paramId")).toString().left(64));
        const int col = intIn(c.value(QStringLiteral("col")), 0, cols - 1, 0);
        const int row = intIn(c.value(QStringLiteral("row")), 0, rows - 1, 0);
        cell.insert(QStringLiteral("col"), col);
        cell.insert(QStringLiteral("row"), row);
        cell.insert(QStringLiteral("colSpan"), intIn(c.value(QStringLiteral("colSpan")), 1, cols - col, 1));
        cell.insert(QStringLiteral("rowSpan"), intIn(c.value(QStringLiteral("rowSpan")), 1, rows - row, 1));

        // Props: scalar values only (bool / number / short string). Nested
        // objects or arrays have no meaning to any widget — drop them.
        QVariantMap props;
        const QVariantMap rawProps = c.value(QStringLiteral("props")).toMap();
        for (auto it = rawProps.constBegin(); it != rawProps.constEnd(); ++it) {
            if (it.key().size() > 64) continue;
            const QVariant &v = it.value();
            switch (v.typeId()) {
            case QMetaType::Bool:
            case QMetaType::Int:
            case QMetaType::LongLong:
            case QMetaType::Double:
                props.insert(it.key(), v);
                break;
            case QMetaType::QString:
                props.insert(it.key(), v.toString().left(kMaxPropString));
                break;
            default:
                break;
            }
        }
        cell.insert(QStringLiteral("props"), props);
        cells.append(cell);
    }
    out.insert(QStringLiteral("cells"), cells);
    return out;
}
