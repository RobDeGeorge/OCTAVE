// ErrorToast.qml, the app-wide error notice: shows one message at a time,
// queues the rest, drops a repeat of the message already showing or queued
// last, dismisses on a tap and on its own after durationMs.

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>

#include <memory>
#include "managers/themecatalog.h"

namespace {
const QString kFrontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");
} // namespace

class TstErrorToast : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_engine = std::make_unique<QQmlEngine>();
        m_engine->addImportPath(kFrontend);
        m_engine->rootContext()->setContextProperty("width", 1280);   // read by Spacing.qml
        m_engine->rootContext()->setContextProperty("height", 720);
        m_themes = std::make_unique<ThemeCatalog>(kFrontend + "/themes.json");   // Style.qml's palettes
        m_engine->rootContext()->setContextProperty("themeCatalog", m_themes.get());
        QQmlComponent comp(m_engine.get(), QUrl::fromLocalFile(kFrontend + "/ErrorToast.qml"));
        m_toast.reset(qobject_cast<QQuickItem *>(comp.create()));
        QVERIFY2(m_toast, qPrintable(comp.errorString()));
        m_toast->setProperty("durationMs", 300);
    }

    void cleanup()
    {
        m_toast.reset();
        m_engine.reset();
    }

    void queuesAndDropsRepeats()
    {
        show("OBD connection lost: Adapter stopped responding");
        QCOMPARE(current(), QStringLiteral("OBD connection lost: Adapter stopped responding"));
        show("OBD connection lost: Adapter stopped responding");   // repeat of the one showing
        QCOMPARE(queued(), 0);
        show("Download failed: Song A");
        show("Download failed: Song A");                            // repeat of the last queued
        show("Download failed: Song B");
        QCOMPARE(queued(), 2);

        QMetaObject::invokeMethod(m_toast.get(), "dismiss");
        QCOMPARE(current(), QStringLiteral("Download failed: Song A"));
        QCOMPARE(queued(), 1);
        QMetaObject::invokeMethod(m_toast.get(), "dismiss");
        QMetaObject::invokeMethod(m_toast.get(), "dismiss");
        QCOMPARE(current(), QString());
        QCOMPARE(queued(), 0);
    }

    void emptyMessageIsIgnored()
    {
        show("");
        QCOMPARE(current(), QString());
    }

    void dismissesItselfInOrder()
    {
        show("first");
        show("second");
        QTRY_COMPARE_WITH_TIMEOUT(current(), QStringLiteral("second"), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(current(), QString(), 2000);
    }

    void tapDismisses()
    {
        QQuickWindow window;
        window.resize(1280, 720);
        m_toast->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        m_toast->setProperty("durationMs", 60000);
        show("OBD connection lost");
        QTRY_VERIFY(m_toast->isVisible() && m_toast->width() > 0);
        const QPointF centre = m_toast->mapToScene(QPointF(m_toast->width() / 2, m_toast->height() / 2));
        QTest::mouseClick(&window, Qt::LeftButton, {}, centre.toPoint());
        QCOMPARE(current(), QString());
        m_toast->setParentItem(nullptr);
    }

private:
    void show(const char *message)
    {
        QMetaObject::invokeMethod(m_toast.get(), "show", Q_ARG(QVariant, QString::fromUtf8(message)));
    }
    QString current() const { return m_toast->property("currentMessage").toString(); }
    int queued() const { return m_toast->property("queuedCount").toInt(); }

    std::unique_ptr<ThemeCatalog> m_themes;   // outlives the engine
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickItem> m_toast;
};

QTEST_MAIN(TstErrorToast)
#include "tst_errortoast.moc"
