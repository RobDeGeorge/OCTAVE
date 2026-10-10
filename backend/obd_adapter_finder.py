"""Find, pair, trust and connect a Bluetooth OBD-II adapter from the GUI.

Python peer of src/managers/obdadapterfinder.{h,cpp} + src/platform/bluezpairer.{h,cpp};
QML context property "obdAdapterFinder".

    startScan()        Bluetooth discovery (Classic + BLE). Every named device
                       lands in `devices`; likely OBD adapters (name or BLE
                       service match) are flagged and sorted first.
    connectAdapter(a)  Classic: pair with the adapter's PIN (1234, 0000, ...)
                       answered by OCTAVE itself through a BlueZ agent, trust
                       it, look up its serial port channel, then hand off to
                       OBDManager.connect_direct().

Phase 1 of TODO/obd-adapter-discovery.md: the full flow runs on desktop Linux
(Pi / Orange Pi included). Elsewhere `supported` is False and the QML sheet
falls back to obdManager.availableAdapters.
"""

import re
import sys

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, ClassInfo

from backend.logging_config import get_logger
from backend.obd_transports import make_rfcomm_url

logger = get_logger(__name__)

_SUPPORTED = sys.platform.startswith("linux")

# Discovery runs this long, then stops on its own
_SCAN_MS = 15000
# SDP channel lookup gives up and falls back to channel 1 after this
_SDP_TIMEOUT_MS = 8000
# A legacy-PIN pairing waits on the adapter; BlueZ's own limit is ~30 s
_PAIR_TIMEOUT_MS = 45000

# PINs tried in order; ELM327 clones ship with 1234 or 0000
PINS = ("1234", "0000", "6789", "1111")

# Names the common ELM327-compatible adapters advertise: generic clones
# ("OBDII", "OBD2", "V-LINK", "Android-Vlink"), OBDLink LX/MX+/CX, Vgate iCar,
# Veepeak, BAFX, Konnwei, NEXAS, LELink, Viecar, Kiwi.
_OBD_NAME_RE = re.compile(
    r"obd|elm ?327|\belm\b|v-?link|vgate|icar|veepeak|bafx|konnwei|\bkw ?\d|nexas|"
    r"lelink|viecar|kiwi ?\d|carista|panlong|scan ?tool|\bvlink",
    re.IGNORECASE)

# BLE service UUIDs ELM327 BLE adapters advertise (the three vendor profiles
# android/src/org/octave/app/OctaveOBDBridge.java speaks)
_OBD_SERVICE_UUIDS = {
    "0000FFF0-0000-1000-8000-00805F9B34FB",
    "0000FFE0-0000-1000-8000-00805F9B34FB",
    "0000FFE1-0000-1000-8000-00805F9B34FB",
    "49535343-FE7D-4AE5-8FA9-9FAFD205E455",
}


def looks_like_obd(name, service_uuids=()):
    if _OBD_NAME_RE.search(name or ""):
        return True
    return any(u.strip("{}").upper() in _OBD_SERVICE_UUIDS for u in service_uuids)


def _sort_key(d):
    # rssi 0 = cached (not heard this scan); sort it after live devices
    rssi = d.get("rssi", 0) or -1000
    return (not d.get("likelyObd"), -rssi, d.get("name", ""))


if _SUPPORTED:
    from PySide6.QtDBus import (QDBus, QDBusConnection, QDBusMessage, QDBusObjectPath,
                                QDBusPendingCallWatcher, QDBusVariant)

    _BLUEZ = "org.bluez"
    _AGENT_PATH = "/org/octave/obd_pin_agent"
    _DEVICE_IFACE = "org.bluez.Device1"
    _PROPS_IFACE = "org.freedesktop.DBus.Properties"

    @ClassInfo({"D-Bus Interface": "org.bluez.Agent1"})
    class _BluezPinAgent(QObject):
        """Answers whatever BlueZ asks with the PIN under test. SSP "just
        works" and numeric-comparison requests are accepted (an OBD dongle
        has no screen to compare against)."""

        def __init__(self, parent=None):
            super().__init__(parent)
            self.pin = PINS[0]

        @Slot()
        def Release(self):
            pass

        @Slot(QDBusObjectPath, result=str)
        def RequestPinCode(self, device):
            logger.info(f"[OBD-BT] PIN requested by {device.path()} -> answering {self.pin}")
            return self.pin

        @Slot(QDBusObjectPath, str)
        def DisplayPinCode(self, device, pincode):
            logger.debug(f"[OBD-BT] DisplayPinCode {pincode}")

        @Slot(QDBusObjectPath, result="uint")
        def RequestPasskey(self, device):
            logger.info(f"[OBD-BT] Passkey requested by {device.path()} -> answering {self.pin}")
            return int(self.pin)

        @Slot(QDBusObjectPath, "uint", "ushort")
        def DisplayPasskey(self, device, passkey, entered):
            pass

        @Slot(QDBusObjectPath, "uint")
        def RequestConfirmation(self, device, passkey):
            pass

        @Slot(QDBusObjectPath)
        def RequestAuthorization(self, device):
            pass

        @Slot(QDBusObjectPath, str)
        def AuthorizeService(self, device, uuid):
            pass

        @Slot()
        def Cancel(self):
            pass

    class _BluezPairer(QObject):
        """Pairs (when not already paired) and trusts a Classic device through
        BlueZ over D-Bus. Qt has no way to answer a legacy PIN request on
        BlueZ, so this registers its own org.bluez.Agent1 for the duration of
        the Pair() call and retries the next common PIN on an authentication
        failure. BlueZ routes the pairing callbacks to the agent of the client
        that called Pair(), so the desktop's own agent is never involved.
        Mirrors src/platform/bluezpairer.{h,cpp}."""

        progress = Signal(str)
        # (ok, try_anyway, message). When pairing fails but the adapter was
        # reachable it is still trusted and try_anyway is set: some adapters
        # (NEXAS) accept an RFCOMM connection from a trusted, unpaired host.
        finished = Signal(bool, bool, str)

        def __init__(self, parent=None):
            super().__init__(parent)
            self._agent = _BluezPinAgent(self)
            self._agent_registered = False
            self._mac = ""
            self._device_path = ""
            self._pin_index = 0
            self._watchers = set()

        def is_running(self):
            return bool(self._mac)

        @staticmethod
        def _call(path, iface, method, args, timeout=3000):
            msg = QDBusMessage.createMethodCall(_BLUEZ, path, iface, method)
            msg.setArguments(list(args))
            return QDBusConnection.systemBus().call(msg, QDBus.CallMode.Block, timeout)

        def _find_device_path(self, mac):
            # BlueZ names device objects /org/bluez/hciN/dev_AA_BB_CC_DD_EE_FF
            leaf = "dev_" + mac.upper().replace(":", "_")
            for i in range(4):
                path = f"/org/bluez/hci{i}/{leaf}"
                reply = self._call(path, _PROPS_IFACE, "Get", [_DEVICE_IFACE, "Address"], 2000)
                if reply.type() == QDBusMessage.MessageType.ReplyMessage:
                    return path
            return ""

        def _is_paired(self):
            reply = self._call(self._device_path, _PROPS_IFACE, "Get", [_DEVICE_IFACE, "Paired"])
            if reply.type() != QDBusMessage.MessageType.ReplyMessage or not reply.arguments():
                return False
            value = reply.arguments()[0]
            if isinstance(value, QDBusVariant):
                value = value.variant()
            return bool(value)

        def _register_agent(self):
            if self._agent_registered:
                return True
            bus = QDBusConnection.systemBus()
            if not bus.registerObject(_AGENT_PATH, self._agent, QDBusConnection.RegisterOption.ExportAllSlots):
                logger.warning(f"[OBD-BT] Could not export the PIN agent: {bus.lastError().message()}")
                return False
            reply = self._call("/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                               [QDBusObjectPath(_AGENT_PATH), "KeyboardDisplay"])
            if reply.type() != QDBusMessage.MessageType.ReplyMessage:
                logger.warning(f"[OBD-BT] RegisterAgent failed: {reply.errorName()} {reply.errorMessage()}")
                bus.unregisterObject(_AGENT_PATH)
                return False
            self._agent_registered = True
            return True

        def _unregister_agent(self):
            if not self._agent_registered:
                return
            self._call("/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
                       [QDBusObjectPath(_AGENT_PATH)], 2000)
            QDBusConnection.systemBus().unregisterObject(_AGENT_PATH)
            self._agent_registered = False

        def start(self, mac):
            if self.is_running():
                self.cancel()
            self._mac = mac.upper()
            self._pin_index = 0
            self._device_path = self._find_device_path(self._mac)
            if not self._device_path:
                self._finish(False, False, "The adapter dropped out of range. Scan again with the ignition on.")
                return
            if self._is_paired():
                logger.info(f"[OBD-BT] {self._mac} already paired")
                self._trust_and_finish()
                return
            if not self._register_agent():
                self._finish(False, False, "Couldn't talk to the Bluetooth service (BlueZ). Is bluetoothd running?")
                return
            self._call_pair()

        def _call_pair(self):
            self._agent.pin = PINS[self._pin_index]
            self.progress.emit("Pairing…" if self._pin_index == 0
                               else f"Pairing… trying PIN {self._agent.pin}")
            logger.info(f"[OBD-BT] Pair {self._mac} PIN candidate {self._agent.pin}")
            msg = QDBusMessage.createMethodCall(_BLUEZ, self._device_path, _DEVICE_IFACE, "Pair")
            watcher = QDBusPendingCallWatcher(
                QDBusConnection.systemBus().asyncCall(msg, _PAIR_TIMEOUT_MS), self)
            self._watchers.add(watcher)
            watcher.finished.connect(self._on_pair_reply)

        def _on_pair_reply(self, watcher):
            self._watchers.discard(watcher)
            watcher.deleteLater()
            if not self.is_running():
                return  # cancelled
            if not watcher.isError():
                self._trust_and_finish()
                return
            err = watcher.error().name()
            err_message = watcher.error().message()
            logger.info(f"[OBD-BT] Pair failed: {err} {err_message}")
            if err == "org.bluez.Error.AlreadyExists":
                self._trust_and_finish()
                return
            auth_failed = err in ("org.bluez.Error.AuthenticationFailed",
                                  "org.bluez.Error.AuthenticationRejected")
            if auth_failed and self._pin_index + 1 < len(PINS):
                self._pin_index += 1
                # The adapter needs a moment before it accepts a new attempt
                QTimer.singleShot(1500, lambda: self.is_running() and self._call_pair())
                return

            if auth_failed:
                message = (f"The adapter refused every common PIN ({', '.join(PINS)}). "
                           "Check its manual for the PIN.")
            elif err.endswith("AuthenticationTimeout") or err.endswith("AuthenticationCanceled"):
                message = "Pairing timed out. If the adapter has a pairing button, press it and try again."
            elif err.endswith("ConnectionAttemptFailed") or err.endswith("NoReply"):
                message = "The adapter didn't answer. Turn the ignition on and check the adapter's light."
            elif err.endswith("InProgress"):
                message = "Another pairing is already running. Try again in a few seconds."
            else:
                message = f"Pairing failed: {err_message}"
            # An adapter that refused or ignored pairing may still take a
            # connection from a trusted host; one that never answered or is
            # busy won't
            try_anyway = not err.endswith(("InProgress", "ConnectionAttemptFailed", "NoReply"))
            if try_anyway:
                self._trust()
            self._finish(False, try_anyway, message)

        def _trust(self):
            reply = self._call(self._device_path, _PROPS_IFACE, "Set",
                               [_DEVICE_IFACE, "Trusted", QDBusVariant(True)])
            if reply.type() != QDBusMessage.MessageType.ReplyMessage:
                logger.warning(f"[OBD-BT] Could not trust {self._mac}: {reply.errorMessage()}")
                return False
            return True

        def _trust_and_finish(self):
            self._trust()  # a paired adapter connects even if this fails
            self._finish(True, False, "Paired")

        def _finish(self, ok, try_anyway, message):
            self._unregister_agent()
            self._mac = ""
            self._device_path = ""
            self.finished.emit(ok, try_anyway, message)

        def cancel(self):
            if not self.is_running():
                return
            msg = QDBusMessage.createMethodCall(_BLUEZ, self._device_path, _DEVICE_IFACE, "CancelPairing")
            QDBusConnection.systemBus().call(msg, QDBus.CallMode.NoBlock)
            self._unregister_agent()
            self._mac = ""
            self._device_path = ""


class OBDAdapterFinder(QObject):
    bluetoothOnChanged = Signal()
    scanningChanged = Signal()
    devicesChanged = Signal()
    busyChanged = Signal()
    statusChanged = Signal()

    def __init__(self, obd_manager=None, settings_manager=None, parent=None):
        super().__init__(parent)
        self._obd = None
        self._settings = settings_manager
        self._local = None
        self._agent = None
        self._sdp = None
        self._pairer = None
        self._devices = {}          # by upper-case address
        self._device_list = []
        self._scanning = False
        self._busy_address = ""
        self._busy_name = ""
        self._handed_off = False
        self._saw_connecting = False
        self._pair_error = ""       # pairing failed but we're trying to connect anyway
        self._status = ""
        self._status_is_error = False

        if _SUPPORTED:
            self._init_bluetooth()
        self.set_obd_manager(obd_manager)

    def _init_bluetooth(self):
        try:
            from PySide6.QtBluetooth import QBluetoothDeviceDiscoveryAgent, QBluetoothLocalDevice
        except ImportError as e:
            logger.warning(f"[OBD-BT] QtBluetooth unavailable: {e}")
            return
        self._local = QBluetoothLocalDevice(self)
        self._local.hostModeStateChanged.connect(lambda *_: self.bluetoothOnChanged.emit())

        self._agent = QBluetoothDeviceDiscoveryAgent(self)
        self._agent.setLowEnergyDiscoveryTimeout(_SCAN_MS)
        self._agent.deviceDiscovered.connect(self._on_device_found)
        self._agent.deviceUpdated.connect(lambda info, _fields: self._on_device_found(info))
        self._agent.finished.connect(self._on_scan_finished)
        self._agent.canceled.connect(lambda: self._set_scanning(False))
        self._agent.errorOccurred.connect(self._on_scan_error)

        self._pairer = _BluezPairer(self)
        self._pairer.progress.connect(lambda m: self._set_status(m))
        self._pairer.finished.connect(self._on_paired)

        self._sdp_timeout = QTimer(self)
        self._sdp_timeout.setSingleShot(True)
        self._sdp_timeout.timeout.connect(self._on_sdp_timeout)

    def set_obd_manager(self, obd_manager):
        """main.py installs the OBD manager (real or dev mock) after this is built."""
        if self._obd is not None:
            try:
                self._obd.connectionStatusChanged.disconnect(self._on_obd_status)
            except (RuntimeError, TypeError):
                pass
        self._obd = obd_manager
        if obd_manager is not None and hasattr(obd_manager, "connectionStatusChanged"):
            obd_manager.connectionStatusChanged.connect(self._on_obd_status)

    # ---- Properties ----

    @Property(bool, constant=True)
    def supported(self):
        return _SUPPORTED and self._agent is not None

    def _bluetooth_on(self):
        if self._local is None or not self._local.isValid():
            return False
        from PySide6.QtBluetooth import QBluetoothLocalDevice
        return self._local.hostMode() != QBluetoothLocalDevice.HostMode.HostPoweredOff

    @Property(bool, notify=bluetoothOnChanged)
    def bluetoothOn(self):
        return self._bluetooth_on()

    @Property(bool, notify=scanningChanged)
    def scanning(self):
        return self._scanning

    @Property(list, notify=devicesChanged)
    def devices(self):
        return self._device_list

    @Property(str, notify=busyChanged)
    def busyAddress(self):
        return self._busy_address

    @Property(str, notify=statusChanged)
    def status(self):
        return self._status

    @Property(bool, notify=statusChanged)
    def statusIsError(self):
        return self._status_is_error

    # ---- Scan ----

    @Slot()
    def startScan(self):
        if not self.supported:
            return
        if not self._bluetooth_on():
            self._set_status("Bluetooth is off.", True)
            return
        if self._busy_address and not self._handed_off:
            return  # pairing; discovery would disturb it
        from PySide6.QtBluetooth import QBluetoothDeviceDiscoveryAgent
        self._devices.clear()
        self._rebuild_list()
        self._set_status("Searching for adapters… plug it in and turn the ignition on.")
        M = QBluetoothDeviceDiscoveryAgent.DiscoveryMethod
        self._agent.start(M.ClassicMethod | M.LowEnergyMethod)
        self._set_scanning(self._agent.isActive())

    @Slot()
    def stopScan(self):
        if self._agent is not None and self._agent.isActive():
            self._agent.stop()
        self._set_scanning(False)

    def _on_scan_finished(self):
        self._set_scanning(False)
        if not self._busy_address and not self._status_is_error:
            self._set_status("" if self._device_list
                             else "No Bluetooth devices found. Is the ignition on?")

    def _on_scan_error(self, error):
        from PySide6.QtBluetooth import QBluetoothDeviceDiscoveryAgent
        logger.warning(f"[OBD-BT] Discovery error: {self._agent.errorString()}")
        self._set_scanning(False)
        if error == QBluetoothDeviceDiscoveryAgent.Error.PoweredOffError:
            self._set_status("Bluetooth is off.", True)
        else:
            self._set_status(f"Bluetooth scan failed: {self._agent.errorString()}", True)

    def _on_device_found(self, info):
        from PySide6.QtBluetooth import QBluetoothDeviceInfo, QBluetoothLocalDevice
        address = info.address().toString().upper()
        if not address or address == "00:00:00:00:00:00":
            return
        # BlueZ names an unnamed device after its address ("40-58-99-A6-80-9A")
        name = (info.name() or "").strip()
        if not name or name.upper() == address.replace(":", "-"):
            return

        uuids = [u.toString().strip("{}").upper() for u in info.serviceUuids()]
        d = dict(self._devices.get(address, {}))

        # Dual-mode adapters (NEXAS NexLink) show up as Classic in one report
        # and LE-only in the next; keep every mode seen this scan
        cfg = info.coreConfigurations()
        C = QBluetoothDeviceInfo.CoreConfiguration
        prev_kind = d.get("kind", "")
        classic = ((cfg & C.BaseRateCoreConfiguration) == C.BaseRateCoreConfiguration
                   or prev_kind in ("classic", "dual"))
        ble = ((cfg & C.LowEnergyCoreConfiguration) == C.LowEnergyCoreConfiguration
               or prev_kind in ("ble", "dual"))

        d["address"] = address
        d["name"] = name
        if info.rssi() != 0 or "rssi" not in d:
            d["rssi"] = int(info.rssi())
        d["kind"] = "dual" if (classic and ble) else ("ble" if ble else "classic")
        d["likelyObd"] = looks_like_obd(name, uuids) or d.get("likelyObd", False)
        pairing = self._local.pairingStatus(info.address())
        d["paired"] = pairing != QBluetoothLocalDevice.Pairing.Unpaired
        # BLE ELM327 needs a GATT transport the desktop OBD worker doesn't have yet
        d["connectable"] = classic
        d["note"] = "" if classic else "Bluetooth LE adapters aren't supported on this device yet"
        self._devices[address] = d
        self._rebuild_list()

    def _rebuild_list(self):
        rows = sorted(self._devices.values(), key=_sort_key)
        if rows != self._device_list:
            self._device_list = rows
            self.devicesChanged.emit()

    # ---- Pair + connect ----

    @Slot(str)
    def connectAdapter(self, address):
        if not self.supported:
            return
        addr = (address or "").strip().upper()
        d = self._devices.get(addr)
        if not d:
            self._set_status("Scan again: that adapter is no longer in the list.", True)
            return
        if not d.get("connectable"):
            self._set_status(d.get("note", ""), True)
            return
        if self._busy_address and not self._handed_off:
            return  # one pairing at a time

        # Discovery and pairing on the same controller slow each other down
        self.stopScan()
        self._busy_name = d.get("name", addr)
        self._handed_off = False
        self._set_busy(addr)
        logger.info(f"[OBD-BT] Connect {self._busy_name} {addr}")
        self._set_status("Pairing…")
        self._pairer.start(addr)

    def _on_paired(self, ok, try_anyway, message):
        if not self._busy_address:
            return
        if not ok and not try_anyway:
            self._set_busy("")
            self._set_status(message, True)
            return
        self._pair_error = "" if ok else message
        if ok and self._busy_address in self._devices:
            self._devices[self._busy_address]["paired"] = True
            self._rebuild_list()
        self._look_up_channel()

    def _look_up_channel(self):
        # Most ELM327 clones serve SPP on channel 1, but OBDLink and a few
        # others don't; ask the adapter's SDP record and fall back to 1
        from PySide6.QtBluetooth import (QBluetoothAddress, QBluetoothServiceDiscoveryAgent,
                                         QBluetoothUuid)
        self._set_status("Finding the adapter's serial port…")
        if self._sdp is not None:
            self._sdp.stop()
            self._sdp.deleteLater()
        self._sdp = QBluetoothServiceDiscoveryAgent(self)
        self._sdp.setRemoteAddress(QBluetoothAddress(self._busy_address))
        self._sdp.setUuidFilter(QBluetoothUuid(QBluetoothUuid.ServiceClassUuid.SerialPort))
        self._sdp.serviceDiscovered.connect(self._on_sdp_service)
        self._sdp.finished.connect(self._sdp_fallback)
        self._sdp.errorOccurred.connect(self._on_sdp_error)
        self._sdp_timeout.start(_SDP_TIMEOUT_MS)
        self._sdp.start(QBluetoothServiceDiscoveryAgent.DiscoveryMode.FullDiscovery)

    def _on_sdp_service(self, info):
        channel = info.serverChannel()
        if channel > 0 and self._sdp_timeout.isActive():
            logger.info(f"[OBD-BT] SPP on channel {channel}")
            self._sdp_timeout.stop()
            self._sdp.stop()
            self._hand_off(channel)

    def _on_sdp_error(self, *_):
        logger.info(f"[OBD-BT] SDP lookup failed: {self._sdp.errorString() if self._sdp else ''}")
        self._sdp_fallback()

    def _sdp_fallback(self):
        if self._sdp_timeout.isActive():
            self._sdp_timeout.stop()
            self._hand_off(1)

    def _on_sdp_timeout(self):
        logger.info("[OBD-BT] SDP lookup timed out, using channel 1")
        if self._sdp is not None:
            self._sdp.stop()
        self._hand_off(1)

    def _hand_off(self, channel):
        if not self._busy_address or self._handed_off:
            return
        self._handed_off = True
        self._saw_connecting = False
        self._set_status("Couldn't pair; trying to connect anyway…" if self._pair_error
                         else "Connecting to the vehicle…")
        if self._obd is not None and hasattr(self._obd, "connect_direct"):
            self._obd.connect_direct(self._busy_address, channel)
        # connect_direct saved the URL as an unnamed chip; give it the real name
        if self._settings is not None:
            self._settings.add_obd_saved_adapter(self._busy_name,
                                                 make_rfcomm_url(self._busy_address, channel))

    def _on_obd_status(self, status):
        if not self._busy_address or not self._handed_off:
            return
        # force_connect() tears the old link down ("Disconnected") before it
        # starts; only judge the attempt once it has begun
        if status in ("Connecting", "Reconnecting"):
            self._saw_connecting = True
            return
        if not self._saw_connecting:
            return
        name = self._busy_name
        self._set_busy("")
        if status == "Connected":
            self._set_status(f"Connected to {name}")
        elif status == "No Vehicle":
            self._set_status(f"{name} is connected, but the vehicle isn't answering. Turn the ignition on.")
        elif self._pair_error:
            self._set_status(self._pair_error, True)
        else:
            self._set_status(f"Paired with {name}, but the connection failed ({status}). "
                             "OCTAVE keeps retrying while the ignition is on.", True)

    @Slot()
    def cancel(self):
        if self._pairer is not None:
            self._pairer.cancel()
        if self._sdp is not None:
            self._sdp_timeout.stop()
            self._sdp.stop()
        self.stopScan()
        self._set_busy("")
        self._set_status("")

    @Slot()
    def powerOn(self):
        if self._local is not None and self._local.isValid():
            self._local.powerOn()

    # ---- State helpers ----

    def _set_scanning(self, on):
        if self._scanning != on:
            self._scanning = on
            self.scanningChanged.emit()

    def _set_status(self, message, is_error=False):
        if self._status == message and self._status_is_error == is_error:
            return
        self._status = message
        self._status_is_error = is_error
        if message:
            logger.info(f"[OBD-BT] {message}")
        self.statusChanged.emit()

    def _set_busy(self, address):
        if self._busy_address == address:
            return
        self._busy_address = address
        if not address:
            self._handed_off = False
        self.busyChanged.emit()
