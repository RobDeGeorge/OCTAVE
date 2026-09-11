import time
import threading

from PySide6.QtCore import QObject, Signal, Slot, QTimer, Property

from backend.logging_config import get_logger
logger = get_logger(__name__)

# I2C Configuration
I2C_BUS = 2
PAJ7620_ADDR = 0x73

# Register Addresses
REG_BANK_SEL = 0xEF
REG_CHIP_ID_L = 0x00
REG_CHIP_ID_H = 0x01
REG_GESTURE_0 = 0x43
REG_GESTURE_1 = 0x44

# Gesture Flags (Register 0x43)
GES_RIGHT            = 0x01
GES_LEFT             = 0x02
GES_UP               = 0x04
GES_DOWN             = 0x08
GES_FORWARD          = 0x10
GES_BACKWARD         = 0x20
GES_CLOCKWISE        = 0x40
GES_COUNTERCLOCKWISE = 0x80

# Gesture Flag (Register 0x44)
GES_WAVE = 0x01

GESTURE_NAMES = {
    GES_RIGHT:            "RIGHT",
    GES_LEFT:             "LEFT",
    GES_UP:               "UP",
    GES_DOWN:             "DOWN",
    GES_FORWARD:          "FORWARD",
    GES_BACKWARD:         "BACKWARD",
    GES_CLOCKWISE:        "CLOCKWISE",
    GES_COUNTERCLOCKWISE: "COUNTER-CLOCKWISE",
}

# PAJ7620U2 Initialization Register Table (DFRobot reference, 219 register pairs)
INIT_REGISTER_ARRAY = [
    # Bank 0
    [0xEF, 0x00],
    [0x32, 0x29], [0x33, 0x01], [0x34, 0x00], [0x35, 0x01],
    [0x36, 0x00], [0x37, 0x07], [0x38, 0x17], [0x39, 0x06],
    [0x3A, 0x12], [0x3F, 0x00], [0x40, 0x02], [0x41, 0xFF],
    [0x42, 0x01], [0x46, 0x2D], [0x47, 0x0F], [0x48, 0x3C],
    [0x49, 0x00], [0x4A, 0x1E], [0x4B, 0x00], [0x4C, 0x20],
    [0x4D, 0x00], [0x4E, 0x1A], [0x4F, 0x14], [0x50, 0x00],
    [0x51, 0x10], [0x52, 0x00], [0x5C, 0x02], [0x5D, 0x00],
    [0x5E, 0x10], [0x5F, 0x3F], [0x60, 0x27], [0x61, 0x28],
    [0x62, 0x00], [0x63, 0x03], [0x64, 0xF7], [0x65, 0x03],
    [0x66, 0xD9], [0x67, 0x03], [0x68, 0x01], [0x69, 0xC8],
    [0x6A, 0x40], [0x6D, 0x04], [0x6E, 0x00], [0x6F, 0x00],
    [0x70, 0x80], [0x71, 0x00], [0x72, 0x00], [0x73, 0x00],
    [0x74, 0xF0], [0x75, 0x00], [0x80, 0x42], [0x81, 0x44],
    [0x82, 0x04], [0x83, 0x20], [0x84, 0x20], [0x85, 0x00],
    [0x86, 0x10], [0x87, 0x00], [0x88, 0x05], [0x89, 0x18],
    [0x8A, 0x10], [0x8B, 0x01], [0x8C, 0x37], [0x8D, 0x00],
    [0x8E, 0xF0], [0x8F, 0x81], [0x90, 0x06], [0x91, 0x06],
    [0x92, 0x1E], [0x93, 0x0D], [0x94, 0x0A], [0x95, 0x0A],
    [0x96, 0x0C], [0x97, 0x05], [0x98, 0x0A], [0x99, 0x41],
    [0x9A, 0x14], [0x9B, 0x0A], [0x9C, 0x3F], [0x9D, 0x33],
    [0x9E, 0xAE], [0x9F, 0xF9], [0xA0, 0x48], [0xA1, 0x13],
    [0xA2, 0x10], [0xA3, 0x08], [0xA4, 0x30], [0xA5, 0x19],
    [0xA6, 0x10], [0xA7, 0x08], [0xA8, 0x24], [0xA9, 0x04],
    [0xAA, 0x1E], [0xAB, 0x1E], [0xCC, 0x19], [0xCD, 0x0B],
    [0xCE, 0x13], [0xCF, 0x64], [0xD0, 0x21], [0xD1, 0x0F],
    [0xD2, 0x88], [0xE0, 0x01], [0xE1, 0x04], [0xE2, 0x41],
    [0xE3, 0xD6], [0xE4, 0x00], [0xE5, 0x0C], [0xE6, 0x0A],
    [0xE7, 0x00], [0xE8, 0x00], [0xE9, 0x00], [0xEE, 0x07],
    # Bank 1
    [0xEF, 0x01],
    [0x00, 0x1E], [0x01, 0x1E], [0x02, 0x0F], [0x03, 0x10],
    [0x04, 0x02], [0x05, 0x00], [0x06, 0xB0], [0x07, 0x04],
    [0x08, 0x0D], [0x09, 0x0E], [0x0A, 0x9C], [0x0B, 0x04],
    [0x0C, 0x05], [0x0D, 0x0F], [0x0E, 0x02], [0x0F, 0x12],
    [0x10, 0x02], [0x11, 0x02], [0x12, 0x00], [0x13, 0x01],
    [0x14, 0x05], [0x15, 0x07], [0x16, 0x05], [0x17, 0x07],
    [0x18, 0x01], [0x19, 0x04], [0x1A, 0x05], [0x1B, 0x0C],
    [0x1C, 0x2A], [0x1D, 0x01], [0x1E, 0x00], [0x21, 0x00],
    [0x22, 0x00], [0x23, 0x00], [0x25, 0x01], [0x26, 0x00],
    [0x27, 0x39], [0x28, 0x7F], [0x29, 0x08], [0x30, 0x03],
    [0x31, 0x00], [0x32, 0x1A], [0x33, 0x1A], [0x34, 0x07],
    [0x35, 0x07], [0x36, 0x01], [0x37, 0xFF], [0x38, 0x36],
    [0x39, 0x07], [0x3A, 0x00], [0x3E, 0xFF], [0x3F, 0x00],
    [0x40, 0x77], [0x41, 0x40], [0x42, 0x00], [0x43, 0x30],
    [0x44, 0xA0], [0x45, 0x5C], [0x46, 0x00], [0x47, 0x00],
    [0x48, 0x58], [0x4A, 0x1E], [0x4B, 0x1E], [0x4C, 0x00],
    [0x4D, 0x00], [0x4E, 0xA0], [0x4F, 0x80], [0x50, 0x00],
    [0x51, 0x00], [0x52, 0x00], [0x53, 0x00], [0x54, 0x00],
    [0x57, 0x80], [0x59, 0x10], [0x5A, 0x08], [0x5B, 0x94],
    [0x5C, 0xE8], [0x5D, 0x08], [0x5E, 0x3D], [0x5F, 0x99],
    [0x60, 0x45], [0x61, 0x40], [0x63, 0x2D], [0x64, 0x02],
    [0x65, 0x96], [0x66, 0x00], [0x67, 0x97], [0x68, 0x01],
    [0x69, 0xCD], [0x6A, 0x01], [0x6B, 0xB0], [0x6C, 0x04],
    [0x6D, 0x2C], [0x6E, 0x01], [0x6F, 0x32], [0x71, 0x00],
    [0x72, 0x01], [0x73, 0x35], [0x74, 0x00], [0x75, 0x33],
    [0x76, 0x31], [0x77, 0x01], [0x7C, 0x84], [0x7D, 0x03],
    [0x7E, 0x01],
]

# Debounce cooldown between duplicate gestures
GESTURE_COOLDOWN = 0.3

# Poll interval (~50Hz)
POLL_INTERVAL = 0.02

# DFRobot two-pass wait times (seconds) for compound gestures.
# Forward/backward start as directional, then complete — wait and re-read
# to see if the sensor resolves to a more specific gesture.
GES_FORWARD_WAIT = 0.30
GES_BACKWARD_WAIT = 0.40

# After confirming a gesture, flush the sensor for this long to prevent
# echo/ghost detections from the same hand motion.
GES_POST_FLUSH = 0.10

DEFAULT_GESTURE_MAPPING = {
    "RIGHT": "next_track",
    "LEFT": "previous_track",
    "UP": "volume_up",
    "DOWN": "volume_down",
    "FORWARD": "play_pause",
    "BACKWARD": "mute_toggle",
    "CLOCKWISE": "volume_up",
    "COUNTER-CLOCKWISE": "volume_down",
    "WAVE": "play_pause",
}


class GestureManager(QObject):
    gestureDetected = Signal(str)
    actionTriggered = Signal(str)
    connectionStatusChanged = Signal(str)

    def __init__(self):
        super().__init__()
        self._running = False
        self._thread = None
        self._bus = None
        self._shutting_down = False
        self._enabled = True
        self._gesture_mapping = DEFAULT_GESTURE_MAPPING.copy()
        self._cooldown = GESTURE_COOLDOWN

        # Reconnection state
        self._retry_count = 0
        self._max_retries = 5
        self._retry_delay_ms = 5000

        self._settings_manager = None

        QTimer.singleShot(1000, self._start)

    def connect_settings_manager(self, settings_manager):
        self._settings_manager = settings_manager
        self._enabled = settings_manager.gestureSensorEnabled
        self._cooldown = settings_manager.gestureCooldown / 1000.0
        mapping = settings_manager.get_gesture_mapping_dict()
        if mapping:
            self._gesture_mapping = mapping

    def _schedule_retry(self):
        if self._shutting_down:
            return
        if self._retry_count >= self._max_retries:
            logger.warning(f"GestureSensor: max retries ({self._max_retries}) reached, giving up")
            return
        self._retry_count += 1
        delay = self._retry_delay_ms * self._retry_count
        logger.info(f"GestureSensor: retry {self._retry_count}/{self._max_retries} in {delay}ms")
        QTimer.singleShot(delay, self._start)

    def _cleanup_bus(self):
        if self._bus:
            try:
                self._bus.close()
            except Exception:
                pass
            self._bus = None

    def _start(self):
        if self._shutting_down:
            return
        if not self._enabled:
            logger.info("GestureSensor: disabled in settings, skipping start")
            self.connectionStatusChanged.emit("Disabled")
            return

        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._cleanup_bus()

        try:
            import smbus2 as smbus
        except ImportError:
            try:
                import smbus
            except ImportError:
                logger.warning("GestureSensor: smbus2/smbus not installed, gesture sensor disabled")
                self.connectionStatusChanged.emit("Disconnected")
                return

        try:
            self._bus = smbus.SMBus(I2C_BUS)
            time.sleep(0.05)
            self._wakeup()
            chip_id = self._read_chip_id()
            if chip_id != 0x7620:
                raise RuntimeError(f"Expected chip ID 0x7620, got 0x{chip_id:04X}")
            self._init_registers()
            logger.info(f"GestureSensor: PAJ7620U2 initialized (chip ID: 0x{chip_id:04X})")
        except FileNotFoundError:
            logger.warning(f"GestureSensor: /dev/i2c-{I2C_BUS} not found")
            self.connectionStatusChanged.emit("Disconnected")
            self._cleanup_bus()
            self._schedule_retry()
            return
        except PermissionError:
            logger.warning(f"GestureSensor: permission denied on /dev/i2c-{I2C_BUS}")
            self.connectionStatusChanged.emit("Disconnected")
            self._cleanup_bus()
            return
        except Exception as e:
            logger.error(f"GestureSensor: init failed: {e}")
            self.connectionStatusChanged.emit("Error")
            self._cleanup_bus()
            self._schedule_retry()
            return

        self._retry_count = 0
        self._running = True
        self.connectionStatusChanged.emit("Connected")
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    # ==================== Sensor I2C ====================

    def _wakeup(self):
        try:
            self._bus.write_byte_data(PAJ7620_ADDR, REG_BANK_SEL, 0x00)
            time.sleep(0.01)
        except OSError:
            time.sleep(0.05)
            self._bus.write_byte_data(PAJ7620_ADDR, REG_BANK_SEL, 0x00)
            time.sleep(0.01)

    def _read_chip_id(self):
        self._bus.write_byte_data(PAJ7620_ADDR, REG_BANK_SEL, 0x00)
        id_low = self._bus.read_byte_data(PAJ7620_ADDR, REG_CHIP_ID_L)
        id_high = self._bus.read_byte_data(PAJ7620_ADDR, REG_CHIP_ID_H)
        return (id_high << 8) | id_low

    def _init_registers(self):
        for reg, val in INIT_REGISTER_ARRAY:
            self._bus.write_byte_data(PAJ7620_ADDR, reg, val)
        self._bus.write_byte_data(PAJ7620_ADDR, REG_BANK_SEL, 0x00)

    # ==================== Read Helpers ====================

    def _read_gesture_raw(self):
        """Read both gesture registers in a single I2C block read."""
        data = self._bus.read_i2c_block_data(PAJ7620_ADDR, REG_GESTURE_0, 2)
        return data[0], data[1]

    def _decode_gesture(self, flag0, flag1):
        """Decode raw flags into a gesture name (or None)."""
        if flag0:
            for bit, name in GESTURE_NAMES.items():
                if flag0 & bit:
                    return name
        if flag1 & GES_WAVE:
            return "WAVE"
        return None

    def _flush_sensor(self, duration):
        """Read and discard gesture data to clear residual flags."""
        end = time.monotonic() + duration
        while time.monotonic() < end and self._running:
            try:
                self._bus.read_i2c_block_data(PAJ7620_ADDR, REG_GESTURE_0, 2)
            except Exception:
                break
            time.sleep(0.01)

    # ==================== Read Loop ====================

    def _read_loop(self):
        last_gesture_time = 0.0
        read_count = 0

        while self._running:
            try:
                flag0, flag1 = self._read_gesture_raw()

                if not flag0 and not (flag1 & GES_WAVE):
                    time.sleep(POLL_INTERVAL)
                    continue

                # Cooldown gate — reject everything if too soon
                now = time.monotonic()
                if (now - last_gesture_time) <= self._cooldown:
                    time.sleep(POLL_INTERVAL)
                    continue

                # --- Two-pass disambiguation for forward/backward ---
                # These are compound gestures (hand approaching/leaving) that
                # the sensor often initially reports as directional. Wait and
                # re-read to let the sensor settle on the final gesture.

                gesture = None

                if flag0 & GES_FORWARD:
                    time.sleep(GES_FORWARD_WAIT)
                    if not self._running:
                        break
                    f0, f1 = self._read_gesture_raw()
                    # If a directional gesture followed, use that instead
                    resolved = self._decode_gesture(f0, f1)
                    if resolved and resolved not in ("FORWARD", "BACKWARD"):
                        gesture = resolved
                    else:
                        gesture = "FORWARD"

                elif flag0 & GES_BACKWARD:
                    time.sleep(GES_BACKWARD_WAIT)
                    if not self._running:
                        break
                    f0, f1 = self._read_gesture_raw()
                    resolved = self._decode_gesture(f0, f1)
                    if resolved and resolved not in ("FORWARD", "BACKWARD"):
                        gesture = resolved
                    else:
                        gesture = "BACKWARD"

                else:
                    # Directional / rotation / wave — confirm with a second read
                    # to reject single-sample glitches. The PAJ7620 clears
                    # 0x43/0x44 on read, so a clean gesture re-reads as either
                    # the same flag or nothing; a *different* flag 20 ms later
                    # means the sensor is still settling on some other motion,
                    # so the sample is dropped.
                    time.sleep(0.02)
                    f0_confirm, f1_confirm = self._read_gesture_raw()
                    first = self._decode_gesture(flag0, flag1)
                    second = self._decode_gesture(f0_confirm, f1_confirm)

                    if second is None or second == first:
                        # Agreed, or sensor already cleared itself
                        gesture = first
                    else:
                        logger.debug(f"GestureSensor: discarding {first} — "
                                     f"confirmation read saw {second}")

                if gesture:
                    read_count += 1
                    last_gesture_time = time.monotonic()

                    self.gestureDetected.emit(gesture)

                    action = self._gesture_mapping.get(gesture, "none")
                    if action and action != "none":
                        self.actionTriggered.emit(action)
                        logger.info(f"GestureSensor: {gesture} -> {action} (#{read_count})")

                    # Flush residual data so the same motion doesn't fire twice
                    self._flush_sensor(GES_POST_FLUSH)

                time.sleep(POLL_INTERVAL)

            except Exception as e:
                logger.error(f"GestureSensor: read loop error: {e}")
                self._running = False
                self.connectionStatusChanged.emit("Error")
                self._cleanup_bus()
                self._schedule_retry()
                break

        logger.info(f"GestureSensor: read loop exited after {read_count} gestures")

    # ==================== Properties / Slots ====================

    @Slot(result=str)
    def getConnectionStatus(self):
        return "Connected" if self._running else "Disconnected"

    @Property(str, notify=connectionStatusChanged)
    def connectionStatus(self):
        # Mirrors C++ Q_PROPERTY(connectionStatus) — read by AccessoriesWidget.qml.
        return self.getConnectionStatus()

    @Slot(str, str)
    def setGestureAction(self, gesture, action):
        self._gesture_mapping[gesture] = action
        if self._settings_manager:
            self._settings_manager.save_gesture_mapping(self._gesture_mapping)

    @Slot(str, result=str)
    def getGestureAction(self, gesture):
        return self._gesture_mapping.get(gesture, "none")

    @Slot()
    def resetMappingToDefaults(self):
        self._gesture_mapping = DEFAULT_GESTURE_MAPPING.copy()
        if self._settings_manager:
            self._settings_manager.save_gesture_mapping(self._gesture_mapping)

    @Slot(bool)
    def setEnabled(self, enabled):
        if enabled == self._enabled:
            return
        self._enabled = enabled
        if self._settings_manager:
            self._settings_manager.save_gesture_sensor_enabled(enabled)
        if enabled and not self._running:
            self._retry_count = 0
            self._start()
        elif not enabled and self._running:
            self._running = False
            if self._thread and self._thread.is_alive():
                self._thread.join(timeout=2.0)
            self._thread = None
            self._cleanup_bus()
            self.connectionStatusChanged.emit("Disabled")

    @Slot(int)
    def setCooldown(self, ms):
        self._cooldown = ms / 1000.0

    @Slot(result=bool)
    def isEnabled(self):
        return self._enabled

    # ==================== Cleanup ====================

    @Slot()
    def cleanup(self):
        logger.info("GestureSensor: cleaning up...")
        self._shutting_down = True
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._cleanup_bus()
