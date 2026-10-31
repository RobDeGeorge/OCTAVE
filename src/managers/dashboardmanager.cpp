// dashboardmanager.cpp

#include "dashboardmanager.h"
#include "parametercatalog.h"
#include "../util/dashboardgrid.h"
#include "../util/pathurl.h"

#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
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
    m_previewExpiry.setSingleShot(true);
    m_previewExpiry.setInterval(60 * 1000);
    connect(&m_previewExpiry, &QTimer::timeout, this, &DashboardManager::endPreview);
}

bool DashboardManager::previewDashboard(const QVariantMap &spec)
{
    const QString id = spec.value(QStringLiteral("id")).toString();
    QVariantMap raw = spec;
    raw.insert(QStringLiteral("id"), id.isEmpty() ? QStringLiteral("preview") : id);
    QString error;
    QVariantMap normal = normalizeSpec(raw, SpecUse::Load, &error, m_widgetCatalog);
    if (normal.isEmpty()) {
        qCWarning(lcDashboards).noquote() << "Preview refused:" << error;
        endPreview();
        return false;
    }
    if (id.isEmpty())
        normal.remove(QStringLiteral("id"));
    normal.remove(QStringLiteral("thumbnail"));
    const QVariantMap next{{QStringLiteral("id"), id}, {QStringLiteral("spec"), normal}};
    m_previewExpiry.start();
    if (next == m_preview)
        return true;
    if (m_preview.isEmpty())
        qCInfo(lcDashboards) << "Preview started for" << (id.isEmpty() ? QStringLiteral("a new dashboard") : id);
    m_preview = next;
    emit previewChanged();
    return true;
}

void DashboardManager::endPreview()
{
    m_previewExpiry.stop();
    if (m_preview.isEmpty())
        return;
    m_preview.clear();
    qCInfo(lcDashboards) << "Preview ended";
    emit previewChanged();
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
    QSet<QString> ids = m_builtInIds;   // hidden presets keep their ids
    ids.insert(QLatin1String(kParameterCardsId));
    for (const QVariant &v : m_dashboards)
        ids.insert(v.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

bool DashboardManager::isBuiltIn(const QString &id) const
{
    return findEntry(id).value(QStringLiteral("builtIn")).toBool();
}

// ---------------------------------------------------------------------------
// Hidden built-ins
// ---------------------------------------------------------------------------

QString DashboardManager::hiddenBuiltInsPath() const
{
    if (m_userDir.isEmpty()) return {};
    return m_userDir + QDir::separator() + QStringLiteral(".hidden-builtins");
}

void DashboardManager::loadHiddenBuiltIns()
{
    m_hiddenBuiltIns.clear();
    QFile f(hiddenBuiltInsPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray ids = doc.array();
    for (const QJsonValue &v : ids) {
        if (v.isString() && !v.toString().isEmpty())
            m_hiddenBuiltIns.insert(v.toString());
    }
}

bool DashboardManager::saveHiddenBuiltIns() const
{
    const QString path = hiddenBuiltInsPath();
    if (path.isEmpty()) return false;
    if (m_hiddenBuiltIns.isEmpty())
        return !QFile::exists(path) || QFile::remove(path);

    QStringList ids(m_hiddenBuiltIns.begin(), m_hiddenBuiltIns.end());
    ids.sort();
    ensureUserDir();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qCWarning(lcDashboards) << "Cannot write" << path << ":" << out.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(QJsonArray::fromStringList(ids)).toJson();
    return out.write(data) == data.size() && out.commit();
}

bool DashboardManager::restoreBuiltInDashboards()
{
    if (m_hiddenBuiltIns.isEmpty()) return false;
    m_hiddenBuiltIns.clear();
    if (!saveHiddenBuiltIns()) {
        qCWarning(lcDashboards) << "Failed to clear hidden built-in list";
        loadHiddenBuiltIns();
        return false;
    }
    qCInfo(lcDashboards) << "Restored all built-in dashboards";
    rescanAll();
    return true;
}

QVariant DashboardManager::loadDashboard(const QString &id)
{
    QVariantMap entry = findEntry(id);
    if (entry.isEmpty()) {
        qCWarning(lcDashboards) << "loadDashboard: unknown id" << id;
        setError(QStringLiteral("No dashboard with id %1").arg(id));
        return QVariantMap{};
    }
    const QVariantMap raw = readFullSpec(entry.value(QStringLiteral("path")).toString());
    if (raw.isEmpty()) {
        setError(QStringLiteral("Dashboard %1 could not be read").arg(id));
        return raw;
    }
    // A newer schema goes through untouched: the renderer shows its "needs a
    // newer OCTAVE" placeholder for it
    if (raw.value(QStringLiteral("schema"), 1).toDouble() > kSupportedSchema) {
        setError(QStringLiteral("This dashboard needs a newer version of OCTAVE"));
        return raw;
    }
    QString error;
    const QVariantMap spec = normalizeSpec(raw, SpecUse::Load, &error, m_widgetCatalog);
    if (spec.isEmpty()) {
        qCWarning(lcDashboards) << "loadDashboard:" << id << error;
        setError(error);
        return QVariantMap{};
    }
    setError({});
    return spec;
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
    m_builtInIds.clear();
    QSet<QString> takenIds;
    // Parameter Cards: a user file with this id is skipped like a built-in clash
    takenIds.insert(QLatin1String(kParameterCardsId));
    loadHiddenBuiltIns();

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
                takenIds.insert(id);
                m_builtInIds.insert(id);
                if (m_hiddenBuiltIns.contains(id))
                    continue;   // deleted by the user; id stays reserved
                header.insert(QStringLiteral("builtIn"), true);
                header.insert(QStringLiteral("path"), fi.absoluteFilePath());
                m_dashboards.append(header);
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
    rescanWidgetPresets();
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
    // The page's "show this dashboard when ..." rule, checked as a save would
    const QVariant when = normalizeSpec(full, SpecUse::Load, nullptr).value(QStringLiteral("showWhen"));
    if (when.isValid())
        header.insert(QStringLiteral("showWhen"), when);
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

QString DashboardManager::saveDashboard(const QVariantMap &rawIn)
{
    // A new dashboard from a client that has no id yet: the one the editor
    // would give it (uniqueIdForLabel), so a save is one call
    QVariantMap raw = rawIn;
    if (raw.value(QStringLiteral("id")).toString().isEmpty()) {
        const QString label = raw.value(QStringLiteral("label")).toString().simplified();
        if (!label.isEmpty())
            raw.insert(QStringLiteral("id"), uniqueIdForLabel(label));
    }
    QString error;
    const QVariantMap spec = normalizeSpec(raw, SpecUse::Save, &error, m_widgetCatalog);
    if (spec.isEmpty()) {
        qCWarning(lcDashboards) << "saveDashboard:" << error;
        setError(error);
        return {};
    }
    const QString id = spec.value(QStringLiteral("id")).toString();
    if (m_userDir.isEmpty()) {
        qCWarning(lcDashboards) << "User dashboards dir not configured";
        setError(QStringLiteral("No place to save dashboards"));
        return {};
    }

    // Don't allow saving over a built-in id (hidden ones included).
    if (m_builtInIds.contains(id)) {
        qCWarning(lcDashboards)
            << "Cannot save: id" << id << "collides with a built-in preset";
        setError(QStringLiteral("%1 is a built-in dashboard").arg(id));
        return {};
    }
    if (id == QLatin1String(kParameterCardsId)) {
        qCWarning(lcDashboards) << "Cannot save: id" << id << "is the Parameter Cards entry";
        setError(QStringLiteral("%1 is the Parameter Cards entry").arg(id));
        return {};
    }

    const QString path = m_userDir + QDir::separator() + id + QStringLiteral(".json");
    if (!writeSpec(path, spec)) {
        setError(QStringLiteral("Could not write %1").arg(path));
        return {};
    }

    rescanAll();
    setError({});
    return id;
}

bool DashboardManager::deleteDashboard(const QString &id)
{
    QVariantMap entry = findEntry(id);
    if (entry.isEmpty()) return false;
    if (entry.value(QStringLiteral("builtIn")).toBool()) {
        // Preset files are read-only app resources: hide instead.
        m_hiddenBuiltIns.insert(id);
        if (!saveHiddenBuiltIns()) {
            qCWarning(lcDashboards) << "Failed to hide built-in dashboard:" << id;
            m_hiddenBuiltIns.remove(id);
            return false;
        }
        qCInfo(lcDashboards) << "Hid built-in dashboard:" << id;
        rescanAll();
        return true;
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
    const QString newId = uniqueIdForLabel(finalLabel);

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

QString DashboardManager::uniqueIdForLabel(const QString &label) const
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
constexpr int kMaxThumbnail = 96 * 1024;   // a 320 x 180 JPEG is ~10-20 KB
}

void DashboardManager::setError(const QString &message)
{
    if (m_lastError == message) return;
    m_lastError = message;
    emit lastErrorChanged();
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
    m_lastExportPath = path;
    emit lastExportPathChanged();
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
    QFile f(PathUrl::openablePath(fileOrUrl));
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

void DashboardManager::setImportReport(const QVariantMap &report)
{
    if (m_lastImportReport == report) return;
    m_lastImportReport = report;
    emit lastImportReportChanged();
}

QString DashboardManager::importSpec(const QVariantMap &raw)
{
    if (raw.value(QStringLiteral("kind")).toString() == QLatin1String("widgetPreset")) {
        setImportReport({});
        setShareError(QStringLiteral("That is a widget, not a dashboard: add it from the editor's widget gallery (Mine)"));
        return {};
    }
    QString error;
    NormalizeReport noted;
    QVariantMap spec = normalizeSpec(raw, SpecUse::Import, &error, m_widgetCatalog, &noted);
    if (spec.isEmpty()) {
        setImportReport({});
        setShareError(error);
        return {};
    }
    const QString id = uniqueIdForLabel(spec.value(QStringLiteral("label")).toString());
    spec.insert(QStringLiteral("id"), id);
    const QString saved = saveDashboard(spec);
    if (saved.isEmpty()) {
        setImportReport({});
        setShareError(QStringLiteral("Could not save the imported dashboard"));
        return {};
    }
    QSet<QString> knownTypes;
    for (const QVariant &w : m_widgetCatalog.value(QStringLiteral("widgets")).toList())
        knownTypes.insert(w.toMap().value(QStringLiteral("type")).toString());
    QStringList unknownTypes, paramIds;
    const QVariantList cells = spec.value(QStringLiteral("cells")).toList();
    for (const QVariant &cv : cells) {
        const QVariantMap c = cv.toMap();
        const QString type = c.value(QStringLiteral("type")).toString();
        const QString pid = c.value(QStringLiteral("paramId")).toString();
        if (!knownTypes.isEmpty() && !knownTypes.contains(type) && !unknownTypes.contains(type))
            unknownTypes.append(type);
        if (!pid.isEmpty() && !paramIds.contains(pid))
            paramIds.append(pid);
    }
    setImportReport({{QStringLiteral("id"), saved},
                     {QStringLiteral("label"), spec.value(QStringLiteral("label"))},
                     {QStringLiteral("cells"), cells.size()},
                     {QStringLiteral("droppedCells"), noted.droppedCells},
                     {QStringLiteral("droppedProps"), noted.droppedProps},
                     {QStringLiteral("movedCells"), noted.movedCells},
                     {QStringLiteral("portraitDropped"), noted.portraitDropped},
                     {QStringLiteral("unknownTypes"), unknownTypes},
                     {QStringLiteral("paramIds"), paramIds}});
    qCInfo(lcDashboards) << "Imported dashboard as" << saved;
    setShareError({});
    return saved;
}

void DashboardManager::setWidgetCatalogFile(const QString &absolutePath)
{
    QFile f(absolutePath);
    QJsonParseError parseError{};
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &parseError) : QJsonDocument();
    if (!doc.isObject()) {
        qCWarning(lcDashboards) << "No widget catalog at" << absolutePath << parseError.errorString();
        m_widgetCatalog.clear();
        return;
    }
    QVariantMap catalog = doc.object().toVariantMap();
    catalog.remove(QStringLiteral("_comment"));
    m_widgetCatalog = catalog;
}

void DashboardManager::setTemplatesDir(const QString &absolutePath)
{
    QList<QVariantMap> found;
    const QDir dir(absolutePath);
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile f(dir.absoluteFilePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QVariantMap t = doc.object().toVariantMap();
        if (t.value(QStringLiteral("id")).toString().isEmpty() || t.value(QStringLiteral("label")).toString().isEmpty()) {
            qCWarning(lcDashboards) << "Skipping template without id / label:" << name;
            continue;
        }
        t.remove(QStringLiteral("_comment"));
        found.append(t);
    }
    std::stable_sort(found.begin(), found.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("order")).toInt() < b.value(QStringLiteral("order")).toInt();
    });
    m_templates.clear();
    for (const QVariantMap &t : found)
        m_templates.append(t);
}

QVariantMap DashboardManager::fromTemplate(const QString &id, const QStringList &supported) const
{
    QVariantMap tpl;
    for (const QVariant &t : m_templates)
        if (t.toMap().value(QStringLiteral("id")).toString() == id)
            tpl = t.toMap();
    if (tpl.isEmpty())
        return {};

    const bool carKnown = !supported.isEmpty();
    const QSet<QString> reported(supported.begin(), supported.end());
    QSet<QString> taken;
    QVariantList cells;
    // Cells that name their PID outright take it first
    for (const QVariant &c : tpl.value(QStringLiteral("cells")).toList())
        if (!c.toMap().value(QStringLiteral("paramId")).toString().isEmpty())
            taken.insert(c.toMap().value(QStringLiteral("paramId")).toString());
    for (const QVariant &c : tpl.value(QStringLiteral("cells")).toList()) {
        QVariantMap cell = c.toMap();
        const QVariantMap slot = cell.take(QStringLiteral("slot")).toMap();
        if (!slot.isEmpty() && cell.value(QStringLiteral("paramId")).toString().isEmpty()) {
            QString pick;
            for (const QVariant &p : slot.value(QStringLiteral("prefer")).toList()) {
                const QString pid = p.toString();
                if (!taken.contains(pid) && ParameterCatalog::find(pid)
                    && (!carKnown || reported.contains(pid) || ParameterCatalog::find(pid)->source == ParameterCatalog::Source::Imu)) {
                    pick = pid;
                    break;
                }
            }
            if (pick.isEmpty() && carKnown) {
                const QStringList kinds = slot.value(QStringLiteral("kinds")).toStringList();
                for (const QString &pid : supported) {
                    const ParameterCatalog::Parameter *p = ParameterCatalog::find(pid);
                    if (p && p->aliasOf.isEmpty() && !taken.contains(pid) && kinds.contains(p->kind)) {
                        pick = pid;
                        break;
                    }
                }
            }
            cell.insert(QStringLiteral("paramId"), pick);
            if (!pick.isEmpty())
                taken.insert(pick);
        }
        cells.append(cell);
    }
    return normalizedSpec({{QStringLiteral("schema"), 1},
                           {QStringLiteral("label"), tpl.value(QStringLiteral("label"))},
                           {QStringLiteral("gridColumns"), tpl.value(QStringLiteral("gridColumns"))},
                           {QStringLiteral("gridRows"), tpl.value(QStringLiteral("gridRows"))},
                           {QStringLiteral("cells"), cells}});
}

namespace {
// A known widget's editable prop held to its catalog kind; false = drop it
bool editableValue(const QVariantMap &def, QVariant &v)
{
    const QString kind = def.value(QStringLiteral("kind")).toString();
    const bool number = v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Double;
    if (kind == QLatin1String("bool"))
        return v.typeId() == QMetaType::Bool;
    if (kind == QLatin1String("string"))
        return v.typeId() == QMetaType::QString;
    if (kind == QLatin1String("enum"))
        return v.typeId() == QMetaType::QString && def.value(QStringLiteral("options")).toStringList().contains(v.toString());
    if (kind == QLatin1String("int") || kind == QLatin1String("real")) {
        if (!number)
            return false;
        double d = v.toDouble();
        if (def.contains(QStringLiteral("min")))
            d = qMax(d, def.value(QStringLiteral("min")).toDouble());
        if (def.contains(QStringLiteral("max")))
            d = qMin(d, def.value(QStringLiteral("max")).toDouble());
        if (kind == QLatin1String("int"))
            v = qRound64(d);
        else if (d != v.toDouble())
            v = d;
        return true;
    }
    return true;   // a kind this build does not know: leave it
}
} // namespace

QVariantMap DashboardManager::normalizeSpec(const QVariantMap &raw, SpecUse use, QString *error, const QVariantMap &catalog,
                                            NormalizeReport *report)
{
    NormalizeReport scratch;
    NormalizeReport &noted = report ? *report : scratch;
    // type -> {prop key -> its editableProps entry}
    QHash<QString, QHash<QString, QVariantMap>> editable;
    for (const QVariant &w : catalog.value(QStringLiteral("widgets")).toList()) {
        const QVariantMap widget = w.toMap();
        auto &props = editable[widget.value(QStringLiteral("type")).toString()];
        for (const QVariant &p : widget.value(QStringLiteral("editableProps")).toList())
            props.insert(p.toMap().value(QStringLiteral("key")).toString(), p.toMap());
    }

    auto fail = [error](const QString &msg) { if (error) *error = msg; return QVariantMap{}; };

    // JSON numbers only (no bools, no numeric strings).
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

    // Missing = 1; a bad one is refused on import and read as 1 otherwise
    const int schema = intIn(raw.value(QStringLiteral("schema"), 1), -1, 1000, 0);
    if (schema > kSupportedSchema)
        return fail(QStringLiteral("This dashboard needs a newer version of OCTAVE"));
    if (schema < 1 && use == SpecUse::Import)
        return fail(QStringLiteral("Not an OCTAVE dashboard (bad schema)"));

    // No cell list: refused on import, an empty dashboard otherwise
    const QVariant cellsVar = raw.value(QStringLiteral("cells"));
    if (cellsVar.typeId() != QMetaType::QVariantList && use == SpecUse::Import)
        return fail(QStringLiteral("Not an OCTAVE dashboard (no cells)"));
    QVariantList rawCells = cellsVar.toList();
    // Too many: refused when shared or saved; a file already on disk shows
    // its first kMaxCells rather than nothing
    if (rawCells.size() > kMaxCells) {
        if (use != SpecUse::Load)
            return fail(QStringLiteral("Dashboard has too many widgets"));
        rawCells = rawCells.mid(0, kMaxCells);
    }

    QVariantMap out;
    // The lowest schema that holds it (set again below with a portrait
    // layout), so a plain dashboard stays readable by builds before 2
    out.insert(QStringLiteral("schema"), 1);

    // The id names the file: imports get a new one from the caller, a save
    // or a load keeps it. A save writes <id>.json, so it must be a plain file
    // name (letters, digits, '-' and '_'; new ids are slugs); a load only
    // needs one.
    if (use != SpecUse::Import) {
        const QString id = raw.value(QStringLiteral("id")).toString();
        static const QRegularExpression fileSafe(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
        if (id.isEmpty() || (use == SpecUse::Save && !fileSafe.match(id).hasMatch()))
            return fail(QStringLiteral("Dashboard has no usable id"));
        out.insert(QStringLiteral("id"), id);
    }

    QString label = raw.value(QStringLiteral("label")).toString().simplified().left(kMaxLabelLength);
    if (label.isEmpty()) {
        if (use == SpecUse::Save)
            return fail(QStringLiteral("Dashboard has no name"));
        label = use == SpecUse::Import ? QStringLiteral("Imported dashboard") : out.value(QStringLiteral("id")).toString();
    }
    out.insert(QStringLiteral("label"), label);

    // Not a positive number = the 12 x 6 default, as the renderer reads it
    int cols = intIn(raw.value(QStringLiteral("gridColumns")), 0, kMaxGrid, 0);
    int rows = intIn(raw.value(QStringLiteral("gridRows")), 0, kMaxGrid, 0);
    if (cols < 1) cols = 12;
    if (rows < 1) rows = 6;
    out.insert(QStringLiteral("gridColumns"), cols);
    out.insert(QStringLiteral("gridRows"), rows);
    for (const QString key : {QStringLiteral("margins"), QStringLiteral("spacing")}) {
        // Absent (or null / undefined from QML) = the renderer's default
        if (!raw.value(key).isNull())
            out.insert(key, intIn(raw.value(key), 0, 200, 0));
    }

    // A picture of the dashboard for sharing (thumbnailFromImage): a small
    // base64 JPEG or PNG data URL, or nothing
    const QString thumb = raw.value(QStringLiteral("thumbnail")).toString();
    static const QRegularExpression dataUrl(QStringLiteral("^data:image/(jpeg|png);base64,[A-Za-z0-9+/]+=*$"));
    if (!thumb.isEmpty() && thumb.size() <= kMaxThumbnail && dataUrl.match(thumb).hasMatch())
        out.insert(QStringLiteral("thumbnail"), thumb);
    // The alert banner over the dashboard (on unless false)
    if (raw.value(QStringLiteral("alertBanner")).typeId() == QMetaType::Bool)
        out.insert(QStringLiteral("alertBanner"), raw.value(QStringLiteral("alertBanner")).toBool());
    // Bring this dashboard up when a value passes a limit: {paramId, above
    // and / or below}; anything less is dropped
    const QVariantMap when = raw.value(QStringLiteral("showWhen")).toMap();
    const QString whenId = when.value(QStringLiteral("paramId")).toString();
    if (!whenId.isEmpty() && whenId.size() <= 64) {
        QVariantMap w{{QStringLiteral("paramId"), whenId}};
        for (const QString key : {QStringLiteral("above"), QStringLiteral("below")}) {
            const QVariant v = when.value(key);
            if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Double)
                w.insert(key, qBound(-1e9, v.toDouble(), 1e9));
        }
        if (w.size() > 1)
            out.insert(QStringLiteral("showWhen"), w);
    }

    QVariantList cells;
    QList<int> rawIndexOf;   // kept cell -> its index in the file (for layouts.portrait)
    for (int rawIndex = 0; rawIndex < rawCells.size(); ++rawIndex) {
        const QVariant &cv = rawCells.at(rawIndex);
        if (cv.typeId() != QMetaType::QVariantMap) continue;
        const QVariantMap c = cv.toMap();
        const QString type = c.value(QStringLiteral("type")).toString();
        if (type.isEmpty() || type.size() > 64) continue;

        QVariantMap cell;
        cell.insert(QStringLiteral("type"), type);
        cell.insert(QStringLiteral("paramId"), c.value(QStringLiteral("paramId")).toString().left(64));
        // Off the grid: the widget keeps its size and moves back in (as a
        // drag does); only a span wider than the grid shrinks
        DashboardGrid::Rect r;
        r.colSpan = intIn(c.value(QStringLiteral("colSpan")), 1, cols, 1);
        r.rowSpan = intIn(c.value(QStringLiteral("rowSpan")), 1, rows, 1);
        r.col = intIn(c.value(QStringLiteral("col")), 0, cols - r.colSpan, 0);
        r.row = intIn(c.value(QStringLiteral("row")), 0, rows - r.rowSpan, 0);
        if (r.col != c.value(QStringLiteral("col")).toInt() || r.row != c.value(QStringLiteral("row")).toInt()
                || r.colSpan != c.value(QStringLiteral("colSpan"), 1).toInt()
                || r.rowSpan != c.value(QStringLiteral("rowSpan"), 1).toInt())
            ++noted.movedCells;
        // Cells never overlap. In file order the earlier cell wins; a later
        // one covering it shrinks toward 1x1 (as a new widget does) and is
        // dropped only if even that is taken.
        if (!DashboardGrid::canPlace(cells, cols, rows, r)) {
            r = DashboardGrid::fitAt(cells, cols, rows, r.col, r.row, r.colSpan, r.rowSpan);
            if (!DashboardGrid::canPlace(cells, cols, rows, r)) {
                qCWarning(lcDashboards) << "Dropping" << type << "at" << r.col << r.row
                                        << "in" << label << ": it overlaps another widget";
                noted.droppedCells.append(type);
                continue;
            }
        }
        cell.insert(QStringLiteral("col"), r.col);
        cell.insert(QStringLiteral("row"), r.row);
        cell.insert(QStringLiteral("colSpan"), r.colSpan);
        cell.insert(QStringLiteral("rowSpan"), r.rowSpan);

        // Props: scalar values only (bool / number / short string). Nested
        // objects or arrays have no meaning to any widget — drop them.
        QVariantMap props;
        const QVariantMap rawProps = c.value(QStringLiteral("props")).toMap();
        const QHash<QString, QVariantMap> known = editable.value(type);
        for (auto it = rawProps.constBegin(); it != rawProps.constEnd(); ++it) {
            if (it.key().size() > 64) { ++noted.droppedProps; continue; }
            QVariant v = it.value();
            if (const auto def = known.constFind(it.key()); def != known.constEnd() && !editableValue(*def, v)) {
                ++noted.droppedProps;
                continue;
            }
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
                ++noted.droppedProps;
                break;
            }
        }
        cell.insert(QStringLiteral("props"), props);
        cells.append(cell);
        rawIndexOf.append(rawIndex);
    }
    out.insert(QStringLiteral("cells"), cells);

    // Schema 2: layouts.portrait {gridColumns, gridRows, cells}, one
    // placement {col, row, colSpan, rowSpan} per cell, in the same order.
    // Held to the same rules as the cells; a cell whose placement is
    // missing or taken goes to the nearest free spot of its size, then 1x1;
    // if even that fails the layout is dropped (the renderer derives one).
    const QVariantMap portraitRaw = raw.value(QStringLiteral("layouts")).toMap().value(QStringLiteral("portrait")).toMap();
    if (!portraitRaw.isEmpty()) {
        int pcols = intIn(portraitRaw.value(QStringLiteral("gridColumns")), 0, kMaxGrid, 0);
        int prows = intIn(portraitRaw.value(QStringLiteral("gridRows")), 0, kMaxGrid, 0);
        if (pcols < 1) pcols = rows;
        if (prows < 1) prows = cols;
        const QVariantList rawPlaces = portraitRaw.value(QStringLiteral("cells")).toList();
        QVariantList places;
        QList<int> missing;
        for (int i = 0; i < cells.size(); ++i) {
            const int ri = rawIndexOf.at(i);
            const QVariantMap p = ri < rawPlaces.size() ? rawPlaces.at(ri).toMap() : QVariantMap();
            DashboardGrid::Rect r;
            r.colSpan = intIn(p.value(QStringLiteral("colSpan")), 1, pcols, 1);
            r.rowSpan = intIn(p.value(QStringLiteral("rowSpan")), 1, prows, 1);
            r.col = intIn(p.value(QStringLiteral("col")), 0, pcols - r.colSpan, 0);
            r.row = intIn(p.value(QStringLiteral("row")), 0, prows - r.rowSpan, 0);
            const bool given = !p.isEmpty();
            if (given && !DashboardGrid::canPlace(places, pcols, prows, r))
                r = DashboardGrid::fitAt(places, pcols, prows, r.col, r.row, r.colSpan, r.rowSpan);
            if (!given || !DashboardGrid::canPlace(places, pcols, prows, r)) {
                missing.append(i);
                r = DashboardGrid::Rect{0, 0, 0, 0};   // placed below, overlaps nothing meanwhile
            }
            places.append(QVariantMap{{QStringLiteral("col"), r.col}, {QStringLiteral("row"), r.row},
                                      {QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}});
        }
        bool ok = true;
        for (int i : missing) {
            const QVariantMap c = cells.at(i).toMap();
            DashboardGrid::Rect r{0, 0, qMin(pcols, c.value(QStringLiteral("colSpan")).toInt()),
                                  qMin(prows, c.value(QStringLiteral("rowSpan")).toInt())};
            if (!DashboardGrid::nearestFree(places, pcols, prows, r, i)) {
                r = {0, 0, 1, 1};
                if (!DashboardGrid::nearestFree(places, pcols, prows, r, i)) {
                    ok = false;
                    break;
                }
            }
            places[i] = QVariantMap{{QStringLiteral("col"), r.col}, {QStringLiteral("row"), r.row},
                                    {QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}};
        }
        if (ok) {
            out.insert(QStringLiteral("layouts"),
                       QVariantMap{{QStringLiteral("portrait"),
                                    QVariantMap{{QStringLiteral("gridColumns"), pcols},
                                                {QStringLiteral("gridRows"), prows},
                                                {QStringLiteral("cells"), places}}}});
            out.insert(QStringLiteral("schema"), 2);
        } else {
            qCWarning(lcDashboards) << "Dropping the portrait layout of" << label << ": its widgets don't fit";
            noted.portraitDropped = true;
        }
    }
    return out;
}

QVariantMap DashboardManager::normalizedSpec(const QVariantMap &spec) const
{
    QVariantMap withId = spec;
    if (withId.value(QStringLiteral("id")).toString().isEmpty())
        withId.insert(QStringLiteral("id"), QStringLiteral("draft"));
    const QVariantMap out = normalizeSpec(withId, SpecUse::Load, nullptr, m_widgetCatalog);
    if (out.isEmpty())
        return {};
    QVariantMap result = out;
    if (spec.value(QStringLiteral("id")).toString().isEmpty())
        result.remove(QStringLiteral("id"));
    return result;
}

bool DashboardManager::canPlace(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                                int colSpan, int rowSpan, int exceptIndex) const
{
    return DashboardGrid::canPlace(cells, gridColumns, gridRows, {col, row, colSpan, rowSpan}, exceptIndex);
}

QVariantMap DashboardManager::fitAt(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                                    int colSpan, int rowSpan) const
{
    const DashboardGrid::Rect r = DashboardGrid::fitAt(cells, gridColumns, gridRows, col, row, colSpan, rowSpan);
    return {{QStringLiteral("col"), r.col}, {QStringLiteral("row"), r.row},
            {QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}};
}

QVariantMap DashboardManager::arrange(const QVariantList &cells, int gridColumns, int gridRows, int index,
                                      int col, int row, int colSpan, int rowSpan) const
{
    const DashboardGrid::Arrangement a =
        DashboardGrid::arrange(cells, gridColumns, gridRows, index, {col, row, colSpan, rowSpan});
    QVariantList moved;
    for (int i : a.moved)
        moved.append(i);
    return {{QStringLiteral("ok"), a.ok}, {QStringLiteral("cells"), a.cells}, {QStringLiteral("moved"), moved},
            {QStringLiteral("swapped"), a.swapped}, {QStringLiteral("reason"), a.reason}};
}

QVariantMap DashboardManager::nearestFree(const QVariantList &cells, int gridColumns, int gridRows, int col, int row,
                                          int colSpan, int rowSpan, int exceptIndex) const
{
    DashboardGrid::Rect r{col, row, colSpan, rowSpan};
    const bool ok = DashboardGrid::nearestFree(cells, gridColumns, gridRows, r, exceptIndex);
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("col"), r.col}, {QStringLiteral("row"), r.row},
            {QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}};
}

QVariantMap DashboardManager::derivePortrait(const QVariantList &cells, int gridColumns, int gridRows) const
{
    const DashboardGrid::Portrait p = DashboardGrid::derivePortrait(cells, gridColumns, gridRows);
    QVariantList places;
    for (const DashboardGrid::Rect &r : p.cells)
        places.append(QVariantMap{{QStringLiteral("col"), r.col}, {QStringLiteral("row"), r.row},
                                  {QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}});
    return {{QStringLiteral("gridColumns"), p.cols}, {QStringLiteral("gridRows"), p.rows},
            {QStringLiteral("cells"), places}};
}

QVariantMap DashboardManager::orientedSpec(const QVariantMap &spec, bool portrait) const
{
    if (!portrait || spec.value(QStringLiteral("cells")).typeId() != QMetaType::QVariantList)
        return spec;
    const QVariantList cells = spec.value(QStringLiteral("cells")).toList();
    QVariantMap layout = spec.value(QStringLiteral("layouts")).toMap().value(QStringLiteral("portrait")).toMap();
    if (layout.value(QStringLiteral("cells")).toList().size() != cells.size())
        layout = derivePortrait(cells, spec.value(QStringLiteral("gridColumns"), 12).toInt(),
                                spec.value(QStringLiteral("gridRows"), 6).toInt());
    const QVariantList places = layout.value(QStringLiteral("cells")).toList();
    QVariantList merged;
    for (int i = 0; i < cells.size(); ++i) {
        QVariantMap c = cells.at(i).toMap();
        const QVariantMap p = places.at(i).toMap();
        for (const char *k : {"col", "row", "colSpan", "rowSpan"})
            c.insert(QLatin1String(k), p.value(QLatin1String(k)));
        merged.append(c);
    }
    QVariantMap out = spec;
    out.insert(QStringLiteral("gridColumns"), layout.value(QStringLiteral("gridColumns")));
    out.insert(QStringLiteral("gridRows"), layout.value(QStringLiteral("gridRows")));
    out.insert(QStringLiteral("cells"), merged);
    out.remove(QStringLiteral("layouts"));
    return out;
}

QVariantMap DashboardManager::rescale(const QVariantList &cells, int fromColumns, int fromRows, int toColumns,
                                      int toRows) const
{
    const DashboardGrid::Rescaled r = DashboardGrid::rescale(cells, fromColumns, fromRows, toColumns, toRows);
    return {{QStringLiteral("ok"), r.ok}, {QStringLiteral("cells"), r.cells}, {QStringLiteral("reason"), r.reason}};
}

namespace {
// The catalog's minSize {w, h} (dp) and aspect (w / h) for a type; a type
// without them (or unknown) reads as any size, square
struct SizeHints
{
    double minW = 0;
    double minH = 0;
    double aspect = 1;
};

SizeHints sizeHints(const QVariantMap &catalog, const QString &type)
{
    for (const QVariant &w : catalog.value(QStringLiteral("widgets")).toList()) {
        const QVariantMap m = w.toMap();
        if (m.value(QStringLiteral("type")).toString() != type)
            continue;
        const QVariantMap min = m.value(QStringLiteral("minSize")).toMap();
        SizeHints h;
        h.minW = min.value(QStringLiteral("w")).toDouble();
        h.minH = min.value(QStringLiteral("h")).toDouble();
        h.aspect = m.value(QStringLiteral("aspect"), 1.0).toDouble();
        if (!(h.aspect > 0))
            h.aspect = 1;
        return h;
    }
    return {};
}
} // namespace

QVariantMap DashboardManager::minSpan(const QString &type, int gridColumns, int gridRows, double cellWidth,
                                      double cellHeight, double spacing) const
{
    const SizeHints h = sizeHints(m_widgetCatalog, type);
    const DashboardGrid::Rect r =
        DashboardGrid::minSpan(gridColumns, gridRows, cellWidth, cellHeight, spacing, h.minW, h.minH);
    return {{QStringLiteral("colSpan"), r.colSpan}, {QStringLiteral("rowSpan"), r.rowSpan}};
}

QVariantList DashboardManager::sizeOptions(const QString &type, int gridColumns, int gridRows, double cellWidth,
                                           double cellHeight, double spacing) const
{
    const SizeHints h = sizeHints(m_widgetCatalog, type);
    QVariantList out;
    for (const DashboardGrid::SizeOption &o : DashboardGrid::sizeOptions(gridColumns, gridRows, cellWidth, cellHeight,
                                                                        spacing, h.minW, h.minH, h.aspect))
        out.append(QVariantMap{{QStringLiteral("label"), o.label}, {QStringLiteral("colSpan"), o.colSpan},
                               {QStringLiteral("rowSpan"), o.rowSpan}});
    return out;
}

QString DashboardManager::thumbnailFromImage(const QVariant &image) const
{
    if (image.typeId() != QMetaType::QImage)
        return {};
    QImage img = image.value<QImage>();
    if (img.isNull())
        return {};
    img = img.scaled(QSize(320, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGB32);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    // PNG where a build has no JPEG writer
    const bool isJpeg = img.save(&buffer, "JPEG", 70);
    if (!isJpeg && !img.save(&buffer, "PNG"))
        return {};
    const QString url = (isJpeg ? QStringLiteral("data:image/jpeg;base64,") : QStringLiteral("data:image/png;base64,"))
                        + QString::fromLatin1(jpeg.toBase64());
    return url.size() <= kMaxThumbnail ? url : QString();
}

// ---------------------------------------------------------------------------
// Widget presets (the user's own widgets)
// ---------------------------------------------------------------------------

QString DashboardManager::widgetsDir() const
{
    return m_userDir.isEmpty() ? QString() : QDir::cleanPath(m_userDir + QStringLiteral("/../widgets"));
}

QVariantMap DashboardManager::normalizePreset(const QVariantMap &raw, QString *error) const
{
    auto fail = [error](const QString &msg) { if (error) *error = msg; return QVariantMap{}; };
    const QString type = raw.value(QStringLiteral("baseType")).toString();
    if (type.isEmpty() || type.size() > 64)
        return fail(QStringLiteral("Not an OCTAVE widget (no type)"));
    const QString label = raw.value(QStringLiteral("label")).toString().simplified().left(kMaxLabelLength);
    if (label.isEmpty())
        return fail(QStringLiteral("The widget has no name"));
    // The props by the dashboard rules: a one-cell dashboard of it
    const QVariantMap cell{{QStringLiteral("type"), type},
                           {QStringLiteral("paramId"), raw.value(QStringLiteral("paramId")).toString()},
                           {QStringLiteral("col"), 0}, {QStringLiteral("row"), 0},
                           {QStringLiteral("colSpan"), 1}, {QStringLiteral("rowSpan"), 1},
                           {QStringLiteral("props"), raw.value(QStringLiteral("props")).toMap()}};
    const QVariantMap spec = normalizeSpec({{QStringLiteral("id"), QStringLiteral("preset")},
                                            {QStringLiteral("label"), label},
                                            {QStringLiteral("cells"), QVariantList{cell}}},
                                           SpecUse::Load, error, m_widgetCatalog);
    const QVariantList cells = spec.value(QStringLiteral("cells")).toList();
    if (cells.isEmpty())
        return fail(QStringLiteral("Not an OCTAVE widget"));
    const QVariantMap c = cells.first().toMap();
    return {{QStringLiteral("label"), label},
            {QStringLiteral("baseType"), type},
            {QStringLiteral("paramId"), c.value(QStringLiteral("paramId"))},
            {QStringLiteral("props"), c.value(QStringLiteral("props"))}};
}

void DashboardManager::rescanWidgetPresets()
{
    QVariantList list;
    const QString dir = widgetsDir();
    if (!dir.isEmpty()) {
        const auto files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (fi.size() > kMaxImportBytes) continue;
            QVariantMap p = normalizePreset(readFullSpec(fi.absoluteFilePath()), nullptr);
            if (p.isEmpty()) {
                qCWarning(lcDashboards) << "Skipping widget preset" << fi.fileName();
                continue;
            }
            p.insert(QStringLiteral("id"), fi.completeBaseName());
            list.append(p);
        }
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("label")).toString().compare(
                   b.toMap().value(QStringLiteral("label")).toString(), Qt::CaseInsensitive) < 0;
    });
    if (list == m_widgetPresets) return;
    m_widgetPresets = list;
    emit widgetPresetsChanged();
}

QString DashboardManager::saveWidgetPreset(const QVariantMap &preset)
{
    QString error;
    QVariantMap p = normalizePreset(preset, &error);
    const QString dir = widgetsDir();
    if (p.isEmpty() || dir.isEmpty()) {
        setError(p.isEmpty() ? error : QStringLiteral("No folder for widgets"));
        return {};
    }
    QSet<QString> taken;
    for (const QVariant &v : m_widgetPresets)
        taken.insert(v.toMap().value(QStringLiteral("id")).toString());
    QString id = preset.value(QStringLiteral("id")).toString();
    if (!taken.contains(id)) {
        const QString base = slugify(p.value(QStringLiteral("label")).toString());
        id = base;
        for (int n = 2; taken.contains(id); ++n)
            id = base + QLatin1Char('-') + QString::number(n);
    }
    p.insert(QStringLiteral("schema"), 1);
    p.insert(QStringLiteral("kind"), QStringLiteral("widgetPreset"));
    if (!writeSpec(dir + QLatin1Char('/') + id + QStringLiteral(".json"), p)) {
        setError(QStringLiteral("Could not write the widget"));
        return {};
    }
    setError({});
    rescanWidgetPresets();
    return id;
}

bool DashboardManager::deleteWidgetPreset(const QString &id)
{
    static const QRegularExpression fileSafe(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    if (!fileSafe.match(id).hasMatch() || widgetsDir().isEmpty())
        return false;
    const bool ok = QFile::remove(widgetsDir() + QLatin1Char('/') + id + QStringLiteral(".json"));
    rescanWidgetPresets();
    return ok;
}

QString DashboardManager::widgetPresetText(const QString &id) const
{
    for (const QVariant &v : m_widgetPresets) {
        QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("id")).toString() != id) continue;
        p.remove(QStringLiteral("id"));
        p.insert(QStringLiteral("schema"), 1);
        p.insert(QStringLiteral("kind"), QStringLiteral("widgetPreset"));
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(p)).toJson(QJsonDocument::Compact));
    }
    return {};
}

bool DashboardManager::copyWidgetPresetToClipboard(const QString &id)
{
    const QString text = widgetPresetText(id);
    QClipboard *cb = QGuiApplication::clipboard();
    if (text.isEmpty() || !cb) {
        setShareError(text.isEmpty() ? QStringLiteral("Widget not found") : QStringLiteral("Clipboard unavailable"));
        return false;
    }
    cb->setText(text);
    setShareError({});
    return true;
}

QString DashboardManager::importWidgetPresetFromText(const QString &json)
{
    const QByteArray data = json.trimmed().toUtf8();
    QJsonParseError err;
    const QJsonDocument doc = data.size() <= kMaxImportBytes ? QJsonDocument::fromJson(data, &err) : QJsonDocument();
    const QVariantMap raw = doc.isObject() ? doc.object().toVariantMap() : QVariantMap();
    if (raw.value(QStringLiteral("kind")).toString() != QLatin1String("widgetPreset")) {
        setShareError(raw.contains(QStringLiteral("cells"))
                          ? QStringLiteral("That is a dashboard, not a widget: import it from the dashboard list")
                          : QStringLiteral("Not an OCTAVE widget"));
        return {};
    }
    if (raw.value(QStringLiteral("schema"), 1).toInt() > 1) {
        setShareError(QStringLiteral("This widget needs a newer version of OCTAVE"));
        return {};
    }
    QVariantMap p = raw;
    p.remove(QStringLiteral("id"));   // never overwrites
    const QString id = saveWidgetPreset(p);
    setShareError(id.isEmpty() ? lastError() : QString());
    return id;
}

QString DashboardManager::importWidgetPresetFromClipboard()
{
    QClipboard *cb = QGuiApplication::clipboard();
    const QString text = cb ? cb->text() : QString();
    if (text.trimmed().isEmpty()) {
        setShareError(QStringLiteral("The clipboard is empty"));
        return {};
    }
    return importWidgetPresetFromText(text);
}
