#include "vehiclemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <cmath>

Q_LOGGING_CATEGORY(lcVehicle, "octave.vehicle")

namespace {

const QString kManifest = QStringLiteral("vehicle.json");
const QString kPicture = QStringLiteral("preview.png");
const QString kActiveFile = QStringLiteral("ACTIVE");
const QString kBuiltInName = QStringLiteral("Jeep TJ");

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]{0,47}$"));
    return re.match(id).hasMatch() && id != VehicleManager::kBuiltInId;
}

// A model's file name as a manifest may give it: a plain name in the
// vehicle's own folder, of a format the view loads
bool validModelName(const QString &name)
{
    if (name.isEmpty() || name.size() > 128 || name.startsWith(QLatin1Char('.'))
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    return name.endsWith(QLatin1String(".glb"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".gltf"), Qt::CaseInsensitive);
}

QString cleanName(const QString &name)
{
    QString out = name.simplified();
    out.truncate(60);
    return out;
}

// "My Civic (final).glb" -> "my-civic-final"
QString slugOf(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            out += c;
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
            out += QLatin1Char('-');
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    out.truncate(40);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("vehicle") : out;
}

// Into -180 < a <= 180; 0 for anything that is not a number
double wrapDegrees(double a)
{
    if (!std::isfinite(a))
        return 0;
    a = std::fmod(a, 360.0);
    if (a > 180)
        a -= 360;
    else if (a <= -180)
        a += 360;
    return a;
}

// What a picker hands back, as something QFile opens: a file: URL becomes
// its path; a content: URI (Android) and a plain path stay as they are
QString openablePath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

// The picked file's name, without folders or extension, for a default name
QString baseNameOf(const QString &pathOrUrl)
{
    QString last = QUrl::fromPercentEncoding(pathOrUrl.toUtf8());
    const int cut = qMax(last.lastIndexOf(QLatin1Char('/')), qMax(last.lastIndexOf(QLatin1Char('\\')), last.lastIndexOf(QLatin1Char(':'))));
    last = last.mid(cut + 1);
    const int dot = last.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? last.left(dot) : last;
}

} // namespace

VehicleManager::VehicleManager(QObject *parent)
    : QObject(parent)
{
}

void VehicleManager::setRoot(const QString &dir)
{
    m_root = dir;
    if (!m_root.isEmpty())
        QDir().mkpath(m_root);
    emit vehiclesChanged();
    emit activeChanged();
}

bool VehicleManager::canImport() const
{
#ifdef OCTAVE_WASM
    return false;   // no file picker that gives a file the copy below can read
#else
    return true;
#endif
}

QString VehicleManager::dirOf(const QString &id) const
{
    return m_root + QLatin1Char('/') + id;
}

QList<VehicleManager::Entry> VehicleManager::installed() const
{
    QList<Entry> out;
    if (m_root.isEmpty())
        return out;
    const QFileInfoList dirs = QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &d : dirs) {
        if (out.size() >= kMaxVehicles)
            break;
        const QString id = d.fileName();
        if (!validId(id))
            continue;
        QFile f(d.filePath() + QLatin1Char('/') + kManifest);
        if (f.size() > 64 * 1024 || !f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        Entry e;
        e.id = id;
        e.model = o.value(QLatin1String("model")).toString();
        if (!validModelName(e.model))
            continue;
        const QFileInfo model(d.filePath() + QLatin1Char('/') + e.model);
        if (!model.isFile() || model.isSymLink())
            continue;
        e.name = cleanName(o.value(QLatin1String("name")).toString());
        if (e.name.isEmpty())
            e.name = id;
        e.yaw = wrapDegrees(o.value(QLatin1String("yaw")).toDouble());
        e.tilt = wrapDegrees(o.value(QLatin1String("tilt")).toDouble());
        out.append(e);
    }
    return out;
}

bool VehicleManager::find(const QString &id, Entry *out) const
{
    for (const Entry &e : installed()) {
        if (e.id == id) {
            if (out)
                *out = e;
            return true;
        }
    }
    return false;
}

bool VehicleManager::writeManifest(const Entry &e) const
{
    QSaveFile f(dirOf(e.id) + QLatin1Char('/') + kManifest);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject o{{"name", e.name}, {"model", e.model}, {"yaw", e.yaw}, {"tilt", e.tilt}};
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

QString VehicleManager::pictureUrlOf(const QString &id) const
{
    const QFileInfo p(dirOf(id) + QLatin1Char('/') + kPicture);
    if (!p.isFile() || p.size() == 0)
        return {};
    // The time in the URL makes an Image load the new picture after a re-render
    QUrl url = QUrl::fromLocalFile(p.filePath());
    url.setQuery(QStringLiteral("t=%1").arg(p.lastModified().toMSecsSinceEpoch()));
    return url.toString();
}

QVariantMap VehicleManager::describe(const Entry &e) const
{
    return {{"id", e.id}, {"name", e.name}, {"builtIn", false}, {"rigged", false},
            {"modelUrl", QUrl::fromLocalFile(dirOf(e.id) + QLatin1Char('/') + e.model).toString()},
            {"pictureUrl", pictureUrlOf(e.id)}, {"yaw", e.yaw}, {"tilt", e.tilt}};
}

QString VehicleManager::activeId() const
{
    if (m_root.isEmpty())
        return kBuiltInId;
    QFile f(m_root + QLatin1Char('/') + kActiveFile);
    if (f.size() > 256 || !f.open(QIODevice::ReadOnly))
        return kBuiltInId;
    const QString id = QString::fromUtf8(f.readAll()).trimmed();
    return validId(id) && find(id, nullptr) ? id : kBuiltInId;
}

QVariantMap VehicleManager::active() const
{
    Entry e;
    if (find(activeId(), &e))
        return describe(e);
    return {{"id", kBuiltInId}, {"name", kBuiltInName}, {"builtIn", true}, {"rigged", true},
            {"modelUrl", QString()}, {"pictureUrl", QString()}, {"yaw", 0.0}, {"tilt", 0.0}};
}

QVariantList VehicleManager::vehicles() const
{
    const QString current = activeId();
    QVariantList out;
    out.append(QVariantMap{{"id", kBuiltInId}, {"name", kBuiltInName}, {"builtIn", true}, {"rigged", true},
                           {"active", current == kBuiltInId}, {"pictureUrl", QString()}});
    for (const Entry &e : installed())
        out.append(QVariantMap{{"id", e.id}, {"name", e.name}, {"builtIn", false}, {"rigged", false},
                               {"active", current == e.id}, {"pictureUrl", pictureUrlOf(e.id)}});
    return out;
}

bool VehicleManager::fail(const QString &why)
{
    qCWarning(lcVehicle) << why;
    if (m_lastError != why) {
        m_lastError = why;
        emit lastErrorChanged();
    }
    return false;
}

void VehicleManager::ok()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
}

QString VehicleManager::importModel(const QString &path, const QString &name)
{
    if (m_root.isEmpty() || !canImport()) {
        fail(QStringLiteral("Models can't be added on this device."));
        return {};
    }
    if (installed().size() >= kMaxVehicles) {
        fail(QStringLiteral("There are already %1 vehicles; remove one first.").arg(kMaxVehicles));
        return {};
    }
    QFile in(openablePath(path));
    if (!in.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("That file can't be opened."));
        return {};
    }
    // A .glb says so in its first four bytes, whatever the file is called
    // (an Android picker's URI carries no extension)
    if (in.peek(4) != QByteArray("glTF")) {
        fail(QStringLiteral("That is not a .glb model. Export the model as glTF binary (.glb) and pick that file."));
        return {};
    }
    if (in.size() > kMaxModelBytes) {
        fail(QStringLiteral("That model is larger than %1 MB.").arg(kMaxModelBytes / (1024 * 1024)));
        return {};
    }

    Entry e;
    e.name = cleanName(name);
    if (e.name.isEmpty())
        e.name = cleanName(baseNameOf(path));
    if (e.name.isEmpty())
        e.name = QStringLiteral("My vehicle");
    e.model = QStringLiteral("model.glb");
    const QString base = slugOf(e.name) == kBuiltInId ? QStringLiteral("vehicle") : slugOf(e.name);
    e.id = base;
    for (int n = 2; QFileInfo::exists(dirOf(e.id)); ++n)
        e.id = QStringLiteral("%1-%2").arg(base).arg(n);

    // Copied under a name the list ignores, then moved into place whole
    const QString staging = m_root + QStringLiteral("/.incoming");
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        fail(QStringLiteral("The vehicles folder can't be written."));
        return {};
    }
    QFile out(staging + QLatin1Char('/') + e.model);
    bool copied = out.open(QIODevice::WriteOnly);
    qint64 total = 0;
    while (copied && !in.atEnd()) {
        const QByteArray chunk = in.read(1 << 20);
        total += chunk.size();
        copied = !chunk.isEmpty() && total <= kMaxModelBytes && out.write(chunk) == chunk.size();
    }
    out.close();
    if (!copied || total == 0 || !QDir().rename(staging, dirOf(e.id)) || !writeManifest(e)) {
        QDir(staging).removeRecursively();
        QDir(dirOf(e.id)).removeRecursively();
        fail(QStringLiteral("The model could not be copied."));
        return {};
    }
    ok();
    emit vehiclesChanged();
    select(e.id);
    return e.id;
}

bool VehicleManager::remove(const QString &id)
{
    if (id == kBuiltInId)
        return fail(QStringLiteral("The built-in vehicle can't be removed."));
    if (!validId(id) || !QFileInfo(dirOf(id)).isDir())
        return fail(QStringLiteral("There is no vehicle named %1.").arg(id));
    const bool wasActive = activeId() == id;
    if (!QDir(dirOf(id)).removeRecursively())
        return fail(QStringLiteral("That vehicle's folder could not be deleted."));
    ok();
    if (wasActive)
        QFile::remove(m_root + QLatin1Char('/') + kActiveFile);
    emit vehiclesChanged();
    if (wasActive)
        emit activeChanged();
    return true;
}

bool VehicleManager::select(const QString &id)
{
    const QString want = id.isEmpty() ? kBuiltInId : id;
    if (want != kBuiltInId && !find(want, nullptr))
        return fail(QStringLiteral("There is no vehicle named %1.").arg(id));
    if (m_root.isEmpty())
        return want == kBuiltInId || fail(QStringLiteral("The vehicles folder is not set."));
    const bool changed = activeId() != want;
    const QString file = m_root + QLatin1Char('/') + kActiveFile;
    if (want == kBuiltInId) {
        QFile::remove(file);
    } else {
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly) || f.write(want.toUtf8()) < 0 || !f.commit())
            return fail(QStringLiteral("The choice could not be saved."));
    }
    ok();
    if (changed) {
        emit activeChanged();
        emit vehiclesChanged();   // each row's `active`
    }
    return true;
}

bool VehicleManager::rename(const QString &id, const QString &name)
{
    Entry e;
    if (!find(id, &e))
        return fail(QStringLiteral("There is no vehicle named %1.").arg(id));
    const QString clean = cleanName(name);
    if (clean.isEmpty())
        return fail(QStringLiteral("A vehicle needs a name."));
    e.name = clean;
    if (!writeManifest(e))
        return fail(QStringLiteral("The name could not be saved."));
    ok();
    emit vehiclesChanged();
    if (activeId() == id)
        emit activeChanged();
    return true;
}

bool VehicleManager::setOrientation(const QString &id, double yaw, double tilt)
{
    Entry e;
    if (!find(id, &e))
        return fail(QStringLiteral("There is no vehicle named %1.").arg(id));
    e.yaw = wrapDegrees(yaw);
    e.tilt = wrapDegrees(tilt);
    if (!writeManifest(e))
        return fail(QStringLiteral("The orientation could not be saved."));
    ok();
    if (activeId() == id)
        emit activeChanged();
    return true;
}

QString VehicleManager::picturePath(const QString &id) const
{
    return find(id, nullptr) ? dirOf(id) + QLatin1Char('/') + kPicture : QString();
}

void VehicleManager::pictureSaved(const QString &id)
{
    if (!find(id, nullptr))
        return;
    emit vehiclesChanged();
    if (activeId() == id)
        emit activeChanged();
}
