// PhoneMirrorView against a fake phoneMirrorManager: the QR screen for "no
// phone", the hint under it, auto-restart when a probe reports the phone back,
// which errors are retried, and the phone picker with two phones.

#include <QJSValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <memory>

class FakeMirror : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceState MEMBER state NOTIFY deviceStateChanged)
    Q_PROPERTY(QString lastErrorKind MEMBER errorKind NOTIFY lastErrorKindChanged)
    Q_PROPERTY(QString deviceHint MEMBER hint NOTIFY deviceStateChanged)
    Q_PROPERTY(QVariantList devices MEMBER deviceRows NOTIFY deviceStateChanged)
    Q_PROPERTY(QString mirrorNotice MEMBER notice NOTIFY mirrorNoticeChanged)
    Q_PROPERTY(bool isRunning MEMBER running NOTIFY isRunningChanged)
    Q_PROPERTY(bool nativeAvailable READ yes CONSTANT)
    Q_PROPERTY(QString serverVersion READ testString CONSTANT)
    Q_PROPERTY(QString adbPath READ adbPath CONSTANT)
    Q_PROPERTY(QString activeDisplaySize READ displaySize NOTIFY frameSizeChanged)
    Q_PROPERTY(bool phoneAsleep READ no NOTIFY phoneAsleepChanged)
    Q_PROPERTY(QString wirelessAddress READ empty NOTIFY wirelessAddressChanged)
    Q_PROPERTY(bool qrPairingActive MEMBER qrActive NOTIFY qrPairingChanged)
    Q_PROPERTY(QStringList qrPairingRows READ noRows NOTIFY qrPairingChanged)
    Q_PROPERTY(QObject *videoSink MEMBER sink NOTIFY videoSinkChanged)

public:
    QString state = QStringLiteral("none");
    QString errorKind;
    QString hint;
    QString notice;
    QVariantList deviceRows;
    bool running = false;
    bool qrActive = false;
    QObject *sink = nullptr;
    int starts = 0;
    QList<int> touchActions;   // injectTouch actions in order (0 down, 1 up, 2 move)

    bool yes() const { return true; }
    bool no() const { return false; }
    QString testString() const { return QStringLiteral("test"); }
    QString adbPath() const { return QStringLiteral("/usr/bin/adb"); }
    QString displaySize() const { return QStringLiteral("1280x720"); }
    QString empty() const { return QString(); }
    QStringList noRows() const { return {}; }

    Q_INVOKABLE bool environmentOk() { return true; }
    Q_INVOKABLE QString getInstallInstructions() { return QString(); }
    Q_INVOKABLE void startScrcpy() { ++starts; }
    Q_INVOKABLE void stopScrcpy() { running = false; }
    Q_INVOKABLE void refreshDeviceState() {}
    Q_INVOKABLE void selectDevice(const QString &) {}
    Q_INVOKABLE void dismissNotice() { notice.clear(); emit mirrorNoticeChanged(); }
    Q_INVOKABLE void setViewSize(int, int) {}
    Q_INVOKABLE void startQrPairing() { qrActive = true; emit qrPairingChanged(); }
    Q_INVOKABLE void cancelQrPairing() { qrActive = false; emit qrPairingChanged(); }
    Q_INVOKABLE void injectTouch(int, int action, float, float) { touchActions << action; }
    Q_INVOKABLE void wakePhone() {}

    // Test drivers
    void probe(const QString &s, const QString &h = QString(), const QVariantList *rows = nullptr)
    {
        state = s;
        hint = h;
        if (rows)
            deviceRows = *rows;
        emit deviceStateChanged();
    }
    // As the manager does: the kind first (errorKindFor), then the signal
    void fail(const QString &kind, const QString &message)
    {
        errorKind = kind;
        emit lastErrorKindChanged();
        emit scrcpyError(message);
    }
    void goLive()
    {
        running = true;
        emit isRunningChanged();
        emit scrcpyStarted(1);
        emit frameReady();
    }

signals:
    void deviceStateChanged();
    void mirrorNoticeChanged();
    void isRunningChanged();
    void scrcpyStarted(int handle);
    void scrcpyStopped();
    void scrcpyError(const QString &message);
    void lastErrorKindChanged();
    void frameReady();
    void frameSizeChanged(int width, int height);
    void wirelessAddressChanged(const QString &address);
    void qrPairingChanged();
    void wirelessResult(bool ok, const QString &message);
    void phoneAsleepChanged(bool asleep);
    void videoSinkChanged();
};

namespace {
const QString kFrontend = QStringLiteral(OCTAVE_SOURCE_DIR "/frontend");

const char *kQml = R"qml(
import QtQuick
import QtQuick.Controls
import "%1" as App

Item {
    id: root
    width: 1280; height: 720
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: App.PhoneMirrorView { objectName: "view"; stackView: stack }
    }
    function prop(name) { return stack.currentItem[name] }
}
)qml";

QVariantMap row(const QString &serial, const QString &state, bool wireless, const QString &model, bool selected)
{
    return {{"serial", serial}, {"state", state}, {"wireless", wireless}, {"model", model}, {"selected", selected}};
}
} // namespace

class TstPhoneMirrorView : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_engine = std::make_unique<QQmlEngine>();
        m_engine->addImportPath(kFrontend);
        m_fake = std::make_unique<FakeMirror>();
        m_engine->rootContext()->setContextProperty("phoneMirrorManager", m_fake.get());
        m_engine->rootContext()->setContextProperty("settingsManager", QVariant::fromValue<QObject *>(nullptr));
        QQmlComponent comp(m_engine.get());
        comp.setData(QString::fromLatin1(kQml).arg(QUrl::fromLocalFile(kFrontend).toString()).toUtf8(), QUrl());
        m_root.reset(comp.create());
        QVERIFY2(m_root, qPrintable(comp.errorString()));
        for (int i = 0; i < 5; ++i)
            QCoreApplication::processEvents();
    }

    void cleanup()
    {
        m_root.reset();
        m_engine.reset();
        m_fake.reset();
    }

    void noPhoneShowsQrWithHintAndRestartsWhenPhoneAppears()
    {
        QCOMPARE(m_fake->starts, 1);   // activation starts the mirror
        m_fake->fail("noPhone", "No Android device connected. Connect via USB and enable USB debugging.");
        m_fake->probe("none", "An iPhone is plugged in. Phone mirroring works with Android phones only.");
        QCoreApplication::processEvents();
        QCOMPARE(prop("noPhone").toBool(), true);
        QCOMPARE(prop("showQr").toBool(), true);
        QVERIFY(m_fake->qrActive);   // QR pairing started for the screen
        QVERIFY(prop("deviceHint").toString().startsWith("An iPhone"));

        // A probe (run off the GUI thread by the backend) reports the phone: restart
        m_fake->probe("device");
        QCoreApplication::processEvents();
        QCOMPARE(m_fake->starts, 2);
        QCOMPARE(prop("launchFailed").toBool(), false);
    }

    void usbPermissionsErrorIsRetried()
    {
        m_fake->fail("device", "A phone is plugged in, but this computer's USB permissions keep adb from opening it.");
        QCoreApplication::processEvents();
        QCOMPARE(prop("deviceError").toBool(), true);   // auto-recovery stays on
        QCOMPARE(prop("showQr").toBool(), false);
        m_fake->probe("device");
        QCoreApplication::processEvents();
        QCOMPARE(m_fake->starts, 2);
    }

    void encoderFailureIsNotRetried()
    {
        m_fake->fail("fatal", "The phone's video encoder failed, even in software mode. Try a smaller virtual display size.");
        QCoreApplication::processEvents();
        QCOMPARE(prop("launchFailed").toBool(), true);
        QCOMPARE(prop("deviceError").toBool(), false);
        m_fake->probe("device");
        QCoreApplication::processEvents();
        QCOMPARE(m_fake->starts, 1);   // no restart loop
    }

    void touchThatStartsInTheLetterboxIsIgnored()
    {
        QQuickWindow window;
        window.resize(1280, 720);
        qobject_cast<QQuickItem *>(m_root.get())->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        m_fake->goLive();
        // A portrait phone in a landscape view: bars left and right of the picture
        auto *sink = qobject_cast<QVideoSink *>(m_fake->sink);
        QVERIFY(sink);
        QVideoFrame frame(QVideoFrameFormat(QSize(360, 720), QVideoFrameFormat::Format_RGBA8888));
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        frame.unmap();
        sink->setVideoFrame(frame);
        QTest::qWait(50);

        // Press in the left bar, drag onto the picture, release: nothing reaches the phone
        QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(100, 360));
        QTest::mouseMove(&window, QPoint(640, 360));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(640, 360));
        QCOMPARE(m_fake->touchActions, QList<int>());

        // Press on the picture, drag into the bar: down, move (clamped), up
        QTest::mousePress(&window, Qt::LeftButton, {}, QPoint(640, 360));
        QTest::mouseMove(&window, QPoint(100, 360));
        QTest::mouseRelease(&window, Qt::LeftButton, {}, QPoint(100, 360));
        QCOMPARE(m_fake->touchActions.first(), 0);
        QCOMPARE(m_fake->touchActions.last(), 1);

        qobject_cast<QQuickItem *>(m_root.get())->setParentItem(nullptr);
    }

    void pickerNeedsTwoUsablePhones()
    {
        m_fake->goLive();
        const QVariantList oneUsable{row("R5CT1", "device", false, "SM-S908U", true),
                                     row("10.0.0.5:5555", "offline", true, "", false)};
        m_fake->probe("device", QString(), &oneUsable);
        QCoreApplication::processEvents();
        QCOMPARE(prop("usableDevices").toList().size(), 1);
        const QVariantList twoUsable{oneUsable.first(), row("PIXEL7", "device", false, "Pixel 7", false)};
        m_fake->probe("device", QString(), &twoUsable);
        QCoreApplication::processEvents();
        QStringList serials;
        for (const QVariant &d : prop("usableDevices").toList())
            serials << d.toMap().value("serial").toString();
        QCOMPARE(serials, (QStringList{"R5CT1", "PIXEL7"}));
    }

private:
    QVariant prop(const char *name)
    {
        QVariant ret;
        QMetaObject::invokeMethod(m_root.get(), "prop", Q_RETURN_ARG(QVariant, ret),
                                  Q_ARG(QVariant, QString::fromLatin1(name)));
        if (ret.metaType() == QMetaType::fromType<QJSValue>())
            ret = ret.value<QJSValue>().toVariant();
        return ret;
    }

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<FakeMirror> m_fake;
    std::unique_ptr<QObject> m_root;
};

QTEST_MAIN(TstPhoneMirrorView)
#include "tst_phonemirrorview.moc"
