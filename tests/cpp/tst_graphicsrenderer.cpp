// GraphicsRenderer::isGpu decides whether Main.qml builds the 3D vehicle page
// in the background (0.9.6-rc1 did so on llvmpipe and blocked the UI for
// 14-19 s on the first visit to Sensor Home). Only positive evidence counts.

#include <QTest>

#include "util/graphicsrenderer.h"

class TstGraphicsRenderer : public QObject
{
    Q_OBJECT

private slots:
    void onlyAKnownGpuCounts_data()
    {
        QTest::addColumn<QString>("description");
        QTest::addColumn<bool>("gpu");
        QTest::newRow("not known yet") << QString() << false;
        QTest::newRow("llvmpipe") << QStringLiteral("OpenGL: llvmpipe (LLVM 15.0.7, 256 bits)") << false;
        QTest::newRow("softpipe") << QStringLiteral("OpenGL: softpipe") << false;
        QTest::newRow("old swrast") << QStringLiteral("OpenGL: Software Rasterizer") << false;
        QTest::newRow("swiftshader") << QStringLiteral("OpenGL: Google SwiftShader") << false;
        QTest::newRow("gdi") << QStringLiteral("OpenGL: GDI Generic") << false;
        QTest::newRow("no name") << QStringLiteral("OpenGL: ?") << false;
        QTest::newRow("empty name") << QStringLiteral("OpenGL: ") << false;
        QTest::newRow("d3d says nothing") << QStringLiteral("Direct3D 11") << false;
        QTest::newRow("vulkan says nothing") << QStringLiteral("Vulkan") << false;
        QTest::newRow("qt quick software") << QStringLiteral("Qt Quick software") << false;
        QTest::newRow("mesa intel") << QStringLiteral("OpenGL: Mesa Intel(R) UHD Graphics 620 (KBL GT2)") << true;
        QTest::newRow("nvidia") << QStringLiteral("OpenGL: NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2") << true;
        QTest::newRow("pi") << QStringLiteral("OpenGL: V3D 7.1.7") << true;
        QTest::newRow("orange pi") << QStringLiteral("OpenGL: Mali-G610 (Panfrost)") << true;
        QTest::newRow("phone") << QStringLiteral("OpenGL: Adreno (TM) 730") << true;
        QTest::newRow("metal") << QStringLiteral("Metal") << true;
    }
    void onlyAKnownGpuCounts()
    {
        QFETCH(QString, description);
        QFETCH(bool, gpu);
        QCOMPARE(GraphicsRenderer::isGpu(description), gpu);
    }
};

QTEST_APPLESS_MAIN(TstGraphicsRenderer)
#include "tst_graphicsrenderer.moc"
