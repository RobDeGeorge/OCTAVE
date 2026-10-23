#include "frontendmanager.h"

#include "../service/protocol.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QtCore/private/qzipreader_p.h>

Q_LOGGING_CATEGORY(lcFrontends, "octave.frontends")

namespace {
const QString kActiveFile = QStringLiteral("ACTIVE");
const QString kUseQmlFile = QStringLiteral("USE_QML");

QString readSmall(const QString &path, qint64 max = 4096)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > max)
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// A zip's central directory as the file has it. QZipReader cleans the
// names it reports ("../x" comes back as "x"), so the checks read the raw
// ones here, with the Unix mode (links, the executable bit) when the zip
// was made on Unix. Empty with *error set for what can't be read (zip64
// included: a frontend that needs it is far over the size limit anyway).
struct ZipEntry {
    QString name;
    quint32 unixMode = 0;   // 0 when not made on Unix
};

QList<ZipEntry> zipDirectory(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("can't read %1").arg(path);
        return {};
    }
    const qint64 tail = qMin<qint64>(f.size(), 22 + 65535);
    f.seek(f.size() - tail);
    const QByteArray end = f.read(tail);
    const int eocd = end.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (eocd < 0 || end.size() - eocd < 22) {
        *error = QStringLiteral("not a zip file");
        return {};
    }
    auto u16 = [](const QByteArray &b, int at) { return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8; };
    auto u32 = [&](const QByteArray &b, int at) { return u16(b, at) | u16(b, at + 2) << 16; };
    const quint32 count = u16(end, eocd + 10);
    const quint32 size = u32(end, eocd + 12);
    const quint32 offset = u32(end, eocd + 16);
    if (count == 0xFFFF || offset == 0xFFFFFFFF || size == 0xFFFFFFFF) {
        *error = QStringLiteral("zip64 archives are not supported");
        return {};
    }
    if (qint64(offset) + size > f.size()) {
        *error = QStringLiteral("a damaged zip file");
        return {};
    }
    f.seek(offset);
    const QByteArray cd = f.read(size);
    QList<ZipEntry> out;
    int at = 0;
    for (quint32 i = 0; i < count; ++i) {
        if (at + 46 > cd.size() || u32(cd, at) != 0x02014b50) {
            *error = QStringLiteral("a damaged zip file");
            return {};
        }
        const int host = quint8(cd[at + 5]);
        const quint32 flags = u16(cd, at + 8);
        const int nameLen = int(u16(cd, at + 28));
        const int extraLen = int(u16(cd, at + 30));
        const int commentLen = int(u16(cd, at + 32));
        const quint32 external = u32(cd, at + 38);
        if (at + 46 + nameLen > cd.size()) {
            *error = QStringLiteral("a damaged zip file");
            return {};
        }
        const QByteArray raw = cd.mid(at + 46, nameLen);
        ZipEntry e;
        e.name = (flags & 0x800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);
        e.unixMode = host == 3 ? external >> 16 : 0;
        out.append(e);
        at += 46 + nameLen + extraLen + commentLen;
    }
    return out;
}

bool writeSmall(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(text.toUtf8() + '\n') > 0;
}
} // namespace

FrontendManager::FrontendManager(QObject *parent)
    : QObject(parent)
{
}

void FrontendManager::setRoot(const QString &dir)
{
    m_root = dir;
    QDir().mkpath(m_root);
    // Leftovers of an install that was cut short
    const QDir root(m_root);
    for (const QString &name : root.entryList({QStringLiteral(".staging-*"), QStringLiteral(".old-*")},
                                              QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
        QDir(root.filePath(name)).removeRecursively();
}

QString FrontendManager::fail(const QString &why)
{
    qCWarning(lcFrontends).noquote() << why;
    m_lastError = why;
    emit lastErrorChanged();
    return {};
}

bool FrontendManager::refuse(const QString &why)
{
    fail(why);
    return false;
}

QString FrontendManager::problemWith(const FrontendManifest &m)
{
    if (!m.speaks(OctaveService::kProtocolVersion))
        return QStringLiteral("it speaks protocol %1-%2; this OCTAVE speaks %3")
            .arg(m.protocolMin).arg(m.protocolMax).arg(OctaveService::kProtocolVersion);
    if (m.isExec()) {
#if !QT_CONFIG(process)
        return QStringLiteral("this build can't start programs");
#else
        if (FrontendManifest::currentPlatform().isEmpty())
            return QStringLiteral("programs can't be the screen on this platform");
        if (m.command().isEmpty())
            return QStringLiteral("it has no program for %1").arg(FrontendManifest::currentPlatform());
#endif
    }
    return {};
}

FrontendManifest FrontendManager::load(const QString &root, const QString &id, QStringList *errors)
{
    const QDir dir(QDir(root).filePath(id));
    QFile f(dir.filePath(QLatin1String(FrontendManifest::kFileName)));
    if (!f.open(QIODevice::ReadOnly)) {
        if (errors)
            *errors << QStringLiteral("no %1").arg(QLatin1String(FrontendManifest::kFileName));
        return {};
    }
    FrontendManifest m = FrontendManifest::parse(f.read(FrontendManifest::kMaxBytes + 1), errors);
    if (m.id.isEmpty())
        return {};
    if (m.id != id) {
        if (errors)
            *errors << QStringLiteral("its folder is %1 but its id is %2").arg(id, m.id);
        return {};
    }
    if (!FrontendManifest::validateFiles(m, dir, errors))
        return {};
    return m;
}

QVariantList FrontendManager::frontends() const
{
    QVariantList out;
    if (m_root.isEmpty())
        return out;
    const QString active = activeId();
    const QDir root(m_root);
    for (const QString &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QStringList errors;
        const FrontendManifest m = load(m_root, id, &errors);
        QVariantMap row = m.id.isEmpty() ? QVariantMap{{QStringLiteral("id"), id}} : m.toMap();
        const QString problem = m.id.isEmpty() ? errors.join(QStringLiteral("; ")) : problemWith(m);
        row.insert(QStringLiteral("active"), id == active);
        row.insert(QStringLiteral("compatible"), problem.isEmpty());
        row.insert(QStringLiteral("problem"), problem);
        row.insert(QStringLiteral("path"), root.filePath(id));
        out.append(row);
    }
    return out;
}

QVariantMap FrontendManager::manifestOf(const QString &id) const
{
    for (const QVariant &v : frontends()) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            return v.toMap();
    }
    return {};
}

QString FrontendManager::activeId() const
{
    if (m_root.isEmpty())
        return {};
    const QString id = readSmall(QDir(m_root).filePath(kActiveFile));
    return QDir(QDir(m_root).filePath(id)).exists() && !id.isEmpty() ? id : QString();
}

bool FrontendManager::forcedQml() const
{
    return !m_root.isEmpty() && QFile::exists(QDir(m_root).filePath(kUseQmlFile));
}

QString FrontendManager::forcedQmlReason() const
{
    return forcedQml() ? readSmall(QDir(m_root).filePath(kUseQmlFile)) : QString();
}

QString FrontendManager::newStaging() const
{
    return QDir(m_root).filePath(QStringLiteral(".staging-%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));
}

QString FrontendManager::installFromFolder(const QString &path)
{
    if (m_root.isEmpty())
        return fail(QStringLiteral("no frontends folder"));
    const QFileInfo source(path);
    if (!source.isDir())
        return fail(QStringLiteral("%1 is not a folder").arg(path));
    const QDir src(source.canonicalFilePath());
    const QString rootPath = QDir(m_root).canonicalPath();
    if (rootPath == src.canonicalPath() || rootPath.startsWith(src.canonicalPath() + QLatin1Char('/'))
        || src.canonicalPath().startsWith(rootPath + QLatin1Char('/')))
        return fail(QStringLiteral("can't install a folder that holds OCTAVE's own frontends"));
    if (!QFile::exists(src.filePath(QLatin1String(FrontendManifest::kFileName))))
        return fail(QStringLiteral("%1 has no %2").arg(path, QLatin1String(FrontendManifest::kFileName)));

    // Every entry checked before anything is copied
    QStringList files;
    qint64 total = 0;
    QDirIterator it(src.path(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = src.relativeFilePath(fi.filePath());
        if (fi.isSymLink())
            return fail(QStringLiteral("%1 is a symbolic link; frontends can't hold links").arg(rel));
        if (rel.count(QLatin1Char('/')) >= kMaxDepth)
            return fail(QStringLiteral("%1 is nested too deep").arg(rel));
        if (fi.isDir())
            continue;
        if (!fi.isFile())
            return fail(QStringLiteral("%1 is not a regular file").arg(rel));
        total += fi.size();
        files << rel;
        if (files.size() > kMaxFiles)
            return fail(QStringLiteral("more than %1 files").arg(kMaxFiles));
        if (total > kMaxBytes)
            return fail(QStringLiteral("more than %1 MB").arg(kMaxBytes / (1024 * 1024)));
    }

    const QString staging = newStaging();
    const QDir dst(staging);
    QDir().mkpath(staging);
    for (const QString &rel : std::as_const(files)) {
        const QString to = dst.filePath(rel);
        QDir().mkpath(QFileInfo(to).path());
        if (!QFile::copy(src.filePath(rel), to)) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("could not copy %1").arg(rel));
        }
        QFile(to).setPermissions(QFile(src.filePath(rel)).permissions());
    }
    return finishInstall(staging);
}

QString FrontendManager::installFromZip(const QString &path)
{
    if (m_root.isEmpty())
        return fail(QStringLiteral("no frontends folder"));
    const QFileInfo source(path);
    if (!source.isFile())
        return fail(QStringLiteral("%1 is not a file").arg(path));
    if (source.size() > kMaxBytes)
        return fail(QStringLiteral("the zip is over %1 MB").arg(kMaxBytes / (1024 * 1024)));
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return fail(QStringLiteral("%1 is not a zip file OCTAVE can read").arg(path));

    QString zipError;
    const QList<ZipEntry> raw = zipDirectory(path, &zipError);
    if (raw.isEmpty())
        return fail(zipError.isEmpty() ? QStringLiteral("the zip is empty") : zipError);
    QHash<QString, quint32> modes;
    for (const ZipEntry &e : raw) {
        QString name = e.name;
        if (name.endsWith(QLatin1Char('/')))
            name.chop(1);
        if (!FrontendManifest::safeRelativePath(name))
            return fail(QStringLiteral("%1 points outside the frontend; refused").arg(e.name));
        if ((e.unixMode & 0170000) == 0120000)
            return fail(QStringLiteral("%1 is a symbolic link; frontends can't hold links").arg(e.name));
        modes.insert(name, e.unixMode);
    }
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    if (entries.size() != raw.size())
        return fail(QStringLiteral("a damaged zip file"));
    // A zip of the folder itself ("dash/octave-frontend.json", ...) or of
    // its contents: strip one common top folder when the manifest is there
    QString prefix;
    bool topManifest = false;
    for (const QZipReader::FileInfo &e : entries)
        topManifest = topManifest || e.filePath == QLatin1String(FrontendManifest::kFileName);
    if (!topManifest && !entries.isEmpty()) {
        const QString first = entries.first().filePath.section(QLatin1Char('/'), 0, 0);
        bool common = !first.isEmpty();
        for (const QZipReader::FileInfo &e : entries)
            common = common && (e.filePath == first || e.filePath.startsWith(first + QLatin1Char('/')));
        if (common)
            prefix = first + QLatin1Char('/');
    }

    qint64 declared = 0;
    int count = 0;
    for (const QZipReader::FileInfo &e : entries) {
        QString name = e.filePath;
        if (name.endsWith(QLatin1Char('/')))
            name.chop(1);
        if (e.isSymLink)
            return fail(QStringLiteral("%1 is a symbolic link; frontends can't hold links").arg(e.filePath));
        if (!FrontendManifest::safeRelativePath(name))
            return fail(QStringLiteral("%1 points outside the frontend; refused").arg(e.filePath));
        if (name.count(QLatin1Char('/')) >= kMaxDepth)
            return fail(QStringLiteral("%1 is nested too deep").arg(e.filePath));
        if (!e.isFile)
            continue;
        declared += e.size;
        if (++count > kMaxFiles)
            return fail(QStringLiteral("more than %1 files").arg(kMaxFiles));
        if (e.size < 0 || declared > kMaxBytes)
            return fail(QStringLiteral("more than %1 MB unpacked").arg(kMaxBytes / (1024 * 1024)));
    }
    if (prefix.isEmpty() && !topManifest)
        return fail(QStringLiteral("the zip has no %1 at its top").arg(QLatin1String(FrontendManifest::kFileName)));

    const QString staging = newStaging();
    const QDir dst(staging);
    QDir().mkpath(staging);
    qint64 written = 0;
    for (const QZipReader::FileInfo &e : entries) {
        if (!e.isFile || (!prefix.isEmpty() && !e.filePath.startsWith(prefix)))
            continue;
        const QString rel = e.filePath.mid(prefix.size());
        const QByteArray data = zip.fileData(e.filePath);
        written += data.size();
        // The sizes in the zip's directory are the zip's word: check what came out
        if (data.size() != e.size || written > kMaxBytes) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("%1 unpacked to a different size than the zip says; refused").arg(e.filePath));
        }
        const QString to = dst.filePath(rel);
        QDir().mkpath(QFileInfo(to).path());
        QFile f(to);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("could not write %1").arg(rel));
        }
        f.close();
        if (modes.value(e.filePath) & 0100)   // the owner's executable bit
            f.setPermissions(f.permissions() | QFile::ExeOwner | QFile::ExeUser);
    }
    return finishInstall(staging);
}

QString FrontendManager::finishInstall(const QString &staging)
{
    const QDir dir(staging);
    QStringList errors;
    QFile mf(dir.filePath(QLatin1String(FrontendManifest::kFileName)));
    FrontendManifest m;
    if (mf.open(QIODevice::ReadOnly))
        m = FrontendManifest::parse(mf.read(FrontendManifest::kMaxBytes + 1), &errors);
    else
        errors << QStringLiteral("no %1").arg(QLatin1String(FrontendManifest::kFileName));
    mf.close();
    if (!m.id.isEmpty())
        FrontendManifest::validateFiles(m, dir, &errors);
    if (errors.isEmpty() && !m.id.isEmpty()) {
        if (const QString problem = problemWith(m); !problem.isEmpty())
            errors << QStringLiteral("can't run here: %1").arg(problem);
    }
    if (!errors.isEmpty() || m.id.isEmpty()) {
        QDir(staging).removeRecursively();
        return fail(QStringLiteral("not installed: %1").arg(errors.join(QStringLiteral("; "))));
    }
    if (m.id == m_runningId) {
        QDir(staging).removeRecursively();
        return fail(QStringLiteral("%1 is running; switch to another screen and restart before replacing it").arg(m.id));
    }

    const QString target = QDir(m_root).filePath(m.id);
    QString old;
    if (QFileInfo::exists(target)) {
        old = QDir(m_root).filePath(QStringLiteral(".old-%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));
        if (!QDir().rename(target, old)) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("could not replace the installed %1").arg(m.id));
        }
    }
    if (!QDir().rename(staging, target)) {
        if (!old.isEmpty())
            QDir().rename(old, target);
        QDir(staging).removeRecursively();
        return fail(QStringLiteral("could not move %1 into place").arg(m.id));
    }
    if (!old.isEmpty())
        QDir(old).removeRecursively();
    qCInfo(lcFrontends).noquote() << "installed" << m.id << m.version << (m.isWeb() ? "(web)" : "(exec)")
                                  << "scopes" << m.scopes.join(QLatin1Char(','));
    m_lastError.clear();
    emit lastErrorChanged();
    emit frontendsChanged();
    emit installed(m.id);
    return m.id;
}

bool FrontendManager::remove(const QString &id)
{
    if (id.isEmpty() || !FrontendManifest::safeRelativePath(id) || id.contains(QLatin1Char('/'))
        || id.startsWith(QLatin1Char('.')) || !QDir(QDir(m_root).filePath(id)).exists())
        return refuse(QStringLiteral("no frontend %1").arg(id));
    if (id == m_runningId)
        return refuse(QStringLiteral("%1 is running; switch to another screen and restart before removing it").arg(id));
    if (activeId() == id)
        QFile::remove(QDir(m_root).filePath(kActiveFile));
    if (!QDir(QDir(m_root).filePath(id)).removeRecursively())
        return refuse(QStringLiteral("could not remove %1").arg(id));
    qCInfo(lcFrontends).noquote() << "removed" << id;
    m_lastError.clear();
    emit lastErrorChanged();
    emit frontendsChanged();
    emit activeChanged();
    return true;
}

bool FrontendManager::setActive(const QString &id)
{
    if (m_root.isEmpty())
        return refuse(QStringLiteral("no frontends folder"));
    if (id.isEmpty()) {
        QFile::remove(QDir(m_root).filePath(kActiveFile));
    } else {
        QStringList errors;
        const FrontendManifest m = (id.startsWith(QLatin1Char('.')) || id.contains(QLatin1Char('/')))
                                       ? FrontendManifest() : load(m_root, id, &errors);
        if (m.id.isEmpty())
            return refuse(QStringLiteral("can't use %1: %2").arg(id, errors.isEmpty() ? QStringLiteral("not installed") : errors.join(QStringLiteral("; "))));
        if (const QString problem = problemWith(m); !problem.isEmpty())
            return refuse(QStringLiteral("can't use %1: %2").arg(id, problem));
        if (!writeSmall(QDir(m_root).filePath(kActiveFile), id))
            return refuse(QStringLiteral("could not save the choice"));
        QFile::remove(QDir(m_root).filePath(kUseQmlFile));   // a fresh choice: try it again
    }
    qCInfo(lcFrontends).noquote() << "active screen from the next start:" << (id.isEmpty() ? QStringLiteral("the QML UI") : id);
    m_lastError.clear();
    emit lastErrorChanged();
    emit activeChanged();
    emit frontendsChanged();
    return true;
}

void FrontendManager::clearForcedQml()
{
    if (QFile::remove(QDir(m_root).filePath(kUseQmlFile)))
        emit activeChanged();
}

FrontendManager::Boot FrontendManager::bootChoice(const QString &root, bool qmlFlag)
{
    Boot boot;
    const QString id = readSmall(QDir(root).filePath(kActiveFile));
    if (id.isEmpty() || id.startsWith(QLatin1Char('.')) || id.contains(QLatin1Char('/')))
        return boot;
    QStringList errors;
    const FrontendManifest m = load(root, id, &errors);
    if (m.id.isEmpty()) {
        boot.note = QStringLiteral("frontend %1 is broken: %2").arg(id, errors.join(QStringLiteral("; ")));
        return boot;
    }
    if (const QString problem = problemWith(m); !problem.isEmpty()) {
        boot.note = QStringLiteral("frontend %1 can't run: %2").arg(id, problem);
        return boot;
    }
    boot.manifest = m;
    boot.dir = QDir(root).filePath(id);
    if (m.isWeb()) {
        boot.kind = Boot::Web;
        return boot;
    }
    if (qmlFlag) {
        boot.note = QStringLiteral("--qml: the QML UI instead of %1 for this run").arg(id);
        return boot;
    }
    if (QFile::exists(QDir(root).filePath(kUseQmlFile))) {
        const QString why = readSmall(QDir(root).filePath(kUseQmlFile));
        boot.note = QStringLiteral("USE_QML: the QML UI instead of %1%2").arg(id, why.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(why));
        return boot;
    }
    boot.kind = Boot::Exec;
    return boot;
}

void FrontendManager::forceQml(const QString &root, const QString &reason)
{
    QDir().mkpath(root);
    writeSmall(QDir(root).filePath(kUseQmlFile), reason);
}
