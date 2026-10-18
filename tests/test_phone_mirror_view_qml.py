"""PhoneMirrorView against a fake phoneMirrorManager, backend-agnostic.

Drives the API both backends implement (deviceState / deviceStateChanged /
refreshDeviceState, scrcpyError, devices, mirrorNotice, deviceHint) and
checks what the view does with it: the QR screen for "no phone", the hint
under it, auto-restart when a probe reports the phone back, the phone
picker with two phones, and the notice banner.
"""
import os

from PySide6.QtCore import Property, QObject, QUrl, Signal, Slot
from PySide6.QtQml import QQmlComponent, QQmlEngine

FRONTEND = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "frontend"))

QML = """
import QtQuick
import QtQuick.Controls
import "%(fe)s" as App

Item {
    id: root
    width: 1280; height: 720
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: App.PhoneMirrorView { objectName: "view"; stackView: stack }
    }
    function view() { return stack.currentItem }
    function prop(name) { return stack.currentItem[name] }
    function _find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; i++) {
            var hit = _find(item.children[i], name)
            if (hit) return hit
        }
        return null
    }
}
"""


class FakeMirror(QObject):
    deviceStateChanged = Signal()
    mirrorNoticeChanged = Signal()
    isRunningChanged = Signal()
    scrcpyStarted = Signal(int)
    scrcpyStopped = Signal()
    scrcpyError = Signal(str)
    frameReady = Signal()
    frameSizeChanged = Signal(int, int)
    wirelessAddressChanged = Signal(str)
    qrPairingChanged = Signal()
    wirelessResult = Signal(bool, str)
    phoneAsleepChanged = Signal(bool)
    videoSinkChanged = Signal()

    def __init__(self):
        super().__init__()
        self.state, self.hint, self.notice = "none", "", ""
        self.device_rows = []
        self.running = False
        self.starts = 0
        self.refreshes = 0
        self.selected = None
        self.qr_active = False
        self.sink = None

    # properties the view binds
    deviceState = Property(str, lambda s: s.state, notify=deviceStateChanged)
    deviceHint = Property(str, lambda s: s.hint, notify=deviceStateChanged)
    devices = Property(list, lambda s: s.device_rows, notify=deviceStateChanged)
    mirrorNotice = Property(str, lambda s: s.notice, notify=mirrorNoticeChanged)
    isRunning = Property(bool, lambda s: s.running, notify=isRunningChanged)
    nativeAvailable = Property(bool, lambda s: True, constant=True)
    serverVersion = Property(str, lambda s: "test", constant=True)
    adbPath = Property(str, lambda s: "/usr/bin/adb", constant=True)
    activeDisplaySize = Property(str, lambda s: "1280x720", notify=frameSizeChanged)
    phoneAsleep = Property(bool, lambda s: False, notify=phoneAsleepChanged)
    wirelessAddress = Property(str, lambda s: "", notify=wirelessAddressChanged)
    qrPairingActive = Property(bool, lambda s: s.qr_active, notify=qrPairingChanged)
    qrPairingRows = Property(list, lambda s: [], notify=qrPairingChanged)

    def _get_sink(self):
        return self.sink

    def _set_sink(self, sink):
        self.sink = sink

    videoSink = Property(QObject, _get_sink, _set_sink, notify=videoSinkChanged)

    @Slot(result=bool)
    def environmentOk(self):
        return True

    @Slot(result=str)
    def getInstallInstructions(self):
        return ""

    @Slot()
    def startScrcpy(self):
        self.starts += 1

    @Slot()
    def stopScrcpy(self):
        self.running = False

    @Slot()
    def refreshDeviceState(self):
        self.refreshes += 1

    @Slot(str)
    def selectDevice(self, serial):
        self.selected = serial

    @Slot()
    def dismissNotice(self):
        self.notice = ""
        self.mirrorNoticeChanged.emit()

    @Slot(int, int)
    def setViewSize(self, w, h):
        pass

    @Slot()
    def startQrPairing(self):
        self.qr_active = True

    @Slot()
    def cancelQrPairing(self):
        self.qr_active = False

    @Slot(int, int, float, float)
    def injectTouch(self, *a):
        pass

    @Slot()
    def wakePhone(self):
        pass

    # test drivers
    def probe(self, state, hint="", rows=None):
        self.state, self.hint = state, hint
        if rows is not None:
            self.device_rows = rows
        self.deviceStateChanged.emit()

    def go_live(self):
        self.running = True
        self.isRunningChanged.emit()
        self.scrcpyStarted.emit(1)
        self.frameReady.emit()


def _setup(qapp):
    engine = QQmlEngine()
    engine.addImportPath(FRONTEND)
    fake = FakeMirror()
    ctx = engine.rootContext()
    ctx.setContextProperty("phoneMirrorManager", fake)
    ctx.setContextProperty("settingsManager", None)
    comp = QQmlComponent(engine)
    comp.setData((QML % {"fe": QUrl.fromLocalFile(FRONTEND).toString()}).encode(), QUrl())
    root = comp.create()
    assert root is not None, comp.errorString()
    for _ in range(5):
        qapp.processEvents()

    def prop(name):
        from PySide6.QtCore import Q_ARG, Q_RETURN_ARG, QMetaObject, Qt
        from PySide6.QtQml import QJSValue
        v = QMetaObject.invokeMethod(root, "prop", Qt.DirectConnection,
                                     Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", name))
        return v.toVariant() if isinstance(v, QJSValue) else v

    root._keep = (engine, comp)   # the root dies with its component
    return engine, root, fake, prop


def test_no_phone_shows_qr_with_hint_and_restarts_when_phone_appears(qapp):
    engine, root, fake, prop = _setup(qapp)
    assert fake.starts == 1                      # activation starts the mirror
    fake.scrcpyError.emit("No Android device connected. Connect via USB and enable USB debugging.")
    fake.probe("none", hint="An iPhone is plugged in. Phone mirroring works with Android phones only.")
    qapp.processEvents()
    assert prop("noPhone") is True
    assert prop("showQr") is True
    assert fake.qr_active is True                # QR pairing started for the screen
    assert prop("deviceHint").startswith("An iPhone")

    # A probe (run off the GUI thread by the backend) reports the phone: restart
    fake.probe("device")
    qapp.processEvents()
    assert fake.starts == 2
    assert prop("launchFailed") is False
    root.deleteLater()
    del engine


def test_usb_permissions_error_is_retried(qapp):
    engine, root, fake, prop = _setup(qapp)
    fake.scrcpyError.emit("A phone is plugged in, but this computer's USB permissions keep adb from opening it.")
    qapp.processEvents()
    assert prop("deviceError") is True           # auto-recovery stays on
    assert prop("showQr") is False
    fake.probe("device")
    qapp.processEvents()
    assert fake.starts == 2
    root.deleteLater()
    del engine


def test_encoder_failure_is_not_retried(qapp):
    engine, root, fake, prop = _setup(qapp)
    fake.scrcpyError.emit("The phone's video encoder failed, even in software mode. Try a smaller virtual display size.")
    qapp.processEvents()
    assert prop("launchFailed") is True
    assert prop("deviceError") is False
    fake.probe("device")
    qapp.processEvents()
    assert fake.starts == 1                      # no restart loop
    root.deleteLater()
    del engine


def test_picker_needs_two_usable_phones(qapp):
    engine, root, fake, prop = _setup(qapp)
    fake.go_live()
    fake.probe("device", rows=[
        {"serial": "R5CT1", "state": "device", "wireless": False, "model": "SM-S908U", "selected": True},
        {"serial": "10.0.0.5:5555", "state": "offline", "wireless": True, "model": "", "selected": False},
    ])
    qapp.processEvents()
    assert len(prop("usableDevices")) == 1
    fake.probe("device", rows=fake.device_rows[:1] + [
        {"serial": "PIXEL7", "state": "device", "wireless": False, "model": "Pixel 7", "selected": False},
    ])
    qapp.processEvents()
    assert [d["serial"] for d in prop("usableDevices")] == ["R5CT1", "PIXEL7"]
    root.deleteLater()
    del engine
