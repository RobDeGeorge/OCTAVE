// Budget checks for the generated music device assets
// (frontend/assets/music_devices/, from tools/music_devices/generate.py).
// They keep a regeneration from quietly bringing back what was optimised
// away: oversized triangle counts, 32-bit index buffers, per-label textures
// instead of one atlas, unreferenced images, and a light probe whose RLE never
// compresses. File parsing only. Ported from tests/test_music_devices_assets.py.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>
#include <QtEndian>

namespace {
const QString kAssets = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend/assets/music_devices");

// Generous ceilings (about 1.5x the generated counts): honest modelling
// changes pass, a regression to the old geometry does not
const QHash<QString, int> kTriangleBudget = {
    {"record-player", 9000}, {"record", 2000}, {"tonearm", 1000},
    {"cassette-player", 4000}, {"cassette", 1200}, {"cassette-door", 1000}, {"reel", 600}, {"tape-pack", 800},
    {"cd-player", 4000}, {"cd", 1800}, {"cd-lid", 1200},
    {"mp3-player", 5000}, {"ipod", 3500},
};
constexpr qint64 kFileBudgetKiB = 200;
const QHash<int, int> kComponentBytes = {{5121, 1}, {5123, 2}, {5125, 4}, {5126, 4}};
const QHash<QString, int> kComponents = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};

struct Glb {
    QJsonObject doc;
    qint64 binSize = 0;
    QString error;
};

Glb loadGlb(const QString &path)
{
    Glb g;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        g.error = f.errorString();
        return g;
    }
    const QByteArray data = f.readAll();
    auto u32 = [&data](qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); };
    if (data.size() < 12 || u32(0) != 0x46546C67 || u32(4) != 2 || u32(8) != quint32(data.size())) {
        g.error = QStringLiteral("not a glTF 2 binary");
        return g;
    }
    for (qsizetype offset = 12; offset + 8 <= data.size();) {
        const quint32 length = u32(offset), type = u32(offset + 4);
        const QByteArray chunk = data.mid(offset + 8, length);
        if (type == 0x4E4F534A)
            g.doc = QJsonDocument::fromJson(chunk).object();
        else if (type == 0x004E4942)
            g.binSize = chunk.size();
        offset += 8 + length;
    }
    return g;
}

QJsonArray primitives(const QJsonObject &doc)
{
    QJsonArray out;
    for (const QJsonValue &mesh : doc.value("meshes").toArray())
        for (const QJsonValue &p : mesh.toObject().value("primitives").toArray())
            out.append(p);
    return out;
}

QSet<int> intSet(int n)
{
    QSet<int> s;
    for (int i = 0; i < n; ++i)
        s.insert(i);
    return s;
}
} // namespace

class TstMusicDeviceAssets : public QObject
{
    Q_OBJECT

private slots:
    void glbs_data()
    {
        QTest::addColumn<QString>("path");
        const QFileInfoList files = QDir(kAssets).entryInfoList({"*.glb"}, QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());
        for (const QFileInfo &fi : files)
            QTest::newRow(qPrintable(fi.completeBaseName())) << fi.absoluteFilePath();
    }

    void glbs()
    {
        QFETCH(QString, path);
        const QString stem = QFileInfo(path).completeBaseName();
        const Glb g = loadGlb(path);
        QVERIFY2(g.error.isEmpty(), qPrintable(g.error));
        const QJsonObject &doc = g.doc;
        const QJsonArray accessors = doc.value("accessors").toArray();
        const QJsonArray views = doc.value("bufferViews").toArray();

        // Geometry budget
        QVERIFY2(kTriangleBudget.contains(stem), qPrintable("add a budget for " + stem));
        int triangles = 0;
        for (const QJsonValue &pv : primitives(doc)) {
            const QJsonObject prim = pv.toObject();
            const QJsonObject indices = accessors.at(prim.value("indices").toInt()).toObject();
            const QJsonObject attrs = prim.value("attributes").toObject();
            const int vertices = accessors.at(attrs.value("POSITION").toInt()).toObject().value("count").toInt();
            const int count = indices.value("count").toInt();
            QCOMPARE(count % 3, 0);
            triangles += count / 3;
            // 16-bit indices unless the part genuinely needs more vertices
            QCOMPARE(indices.value("componentType").toInt(), vertices > 65535 ? 5125 : 5123);

            QList<int> used{prim.value("indices").toInt()};
            for (const QJsonValue &a : attrs)
                used << a.toInt();
            for (int ai : used) {
                const QJsonObject acc = accessors.at(ai).toObject();
                const QJsonObject view = views.at(acc.value("bufferView").toInt()).toObject();
                const qint64 needed = qint64(acc.value("count").toInt())
                                      * kComponentBytes.value(acc.value("componentType").toInt())
                                      * kComponents.value(acc.value("type").toString());
                QVERIFY2(view.value("byteOffset").toInteger() + needed <= g.binSize, "accessor past buffer end");
            }
        }
        QVERIFY2(triangles <= kTriangleBudget.value(stem), qPrintable(QStringLiteral("%1 triangles").arg(triangles)));
        QVERIFY2(QFileInfo(path).size() <= kFileBudgetKiB * 1024,
                 qPrintable(QStringLiteral("%1 KiB").arg(QFileInfo(path).size() / 1024)));

        // Every material, texture and image is used
        const QJsonArray materials = doc.value("materials").toArray();
        QSet<int> usedMaterials;
        for (const QJsonValue &pv : primitives(doc))
            usedMaterials.insert(pv.toObject().value("material").toInt());
        QCOMPARE(usedMaterials, intSet(materials.size()));
        QSet<int> usedTextures;
        int decals = 0;
        for (const QJsonValue &mv : materials) {
            const QJsonObject pbr = mv.toObject().value("pbrMetallicRoughness").toObject();
            if (pbr.contains("baseColorTexture")) {
                usedTextures.insert(pbr.value("baseColorTexture").toObject().value("index").toInt());
                if (mv.toObject().value("alphaMode").toString() == QLatin1String("BLEND"))
                    ++decals;
            }
        }
        const QJsonArray textures = doc.value("textures").toArray();
        QCOMPARE(usedTextures, intSet(textures.size()));
        QSet<int> usedImages;
        for (int t : std::as_const(usedTextures))
            usedImages.insert(textures.at(t).toObject().value("source").toInt());
        QCOMPARE(usedImages, intSet(doc.value("images").toArray().size()));
        // Every printed decal shares one atlas, and every material owns one draw call
        QVERIFY2(decals <= 1, qPrintable(QStringLiteral("%1 separate decal materials").arg(decals)));
        QCOMPARE(doc.value("meshes").toArray().size(), materials.size());
    }

    void lightProbeAndMasks()
    {
        QFile probe(kAssets + "/studio.hdr");
        QVERIFY(probe.open(QIODevice::ReadOnly));
        const QByteArray hdr = probe.readAll();
        QVERIFY(hdr.startsWith("#?RADIANCE"));
        QVERIFY2(hdr.contains("-Y 128 +X 256\n"), "probe resolution changed; update MusicDeviceScene expectations");
        QVERIFY2(hdr.size() < 64 * 1024, "studio.hdr is too big; RLE is not compressing");
        for (const char *name : {"mask-record.png", "mask-cd.png"}) {
            QFile f(kAssets + "/" + name);
            QVERIFY2(f.open(QIODevice::ReadOnly), name);
            const QByteArray png = f.readAll();
            QVERIFY2(png.startsWith("\x89PNG"), name);
            QCOMPARE(qFromBigEndian<quint32>(png.constData() + 16), 512u);
            QCOMPARE(qFromBigEndian<quint32>(png.constData() + 20), 512u);
        }
    }
};

QTEST_GUILESS_MAIN(TstMusicDeviceAssets)
#include "tst_musicdeviceassets.moc"
