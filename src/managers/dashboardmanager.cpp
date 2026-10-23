// dashboardmanager.cpp

#include "dashboardmanager.h"
#include "../util/dashboardgrid.h"
#include "../util/pathurl.h"

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

QString DashboardManager::importSpec(const QVariantMap &raw)
{
    QString error;
    QVariantMap spec = normalizeSpec(raw, SpecUse::Import, &error, m_widgetCatalog);
    if (spec.isEmpty()) {
        setShareError(error);
        return {};
    }
    const QString id = uniqueIdForLabel(spec.value(QStringLiteral("label")).toString());
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

QVariantMap DashboardManager::normalizeSpec(const QVariantMap &raw, SpecUse use, QString *error, const QVariantMap &catalog)
{
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
    out.insert(QStringLiteral("schema"), kSupportedSchema);

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

    QVariantList cells;
    for (const QVariant &cv : rawCells) {
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
        // Cells never overlap. In file order the earlier cell wins; a later
        // one covering it shrinks toward 1x1 (as a new widget does) and is
        // dropped only if even that is taken.
        if (!DashboardGrid::canPlace(cells, cols, rows, r)) {
            r = DashboardGrid::fitAt(cells, cols, rows, r.col, r.row, r.colSpan, r.rowSpan);
            if (!DashboardGrid::canPlace(cells, cols, rows, r)) {
                qCWarning(lcDashboards) << "Dropping" << type << "at" << r.col << r.row
                                        << "in" << label << ": it overlaps another widget";
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
            if (it.key().size() > 64) continue;
            QVariant v = it.value();
            if (const auto def = known.constFind(it.key()); def != known.constEnd() && !editableValue(*def, v))
                continue;
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
