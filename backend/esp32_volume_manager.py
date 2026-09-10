"""
ESP32 Volume Manager for OCTAVE
Handles USB Serial connection to ESP32-S3 receiver dongle for volume control.
The ESP32-S3 receives commands from a remote ESP32 encoder via ESP-NOW.
"""

from PySide6.QtCore import QObject, Signal, Slot, QTimer, Property
import serial
import serial.tools.list_ports
import threading
import sys
import glob
import time

from backend.logging_config import get_logger
logger = get_logger(__name__)


class ESP32VolumeManager(QObject):
    """
    Manager for ESP32 rotary encoder volume control via USB Serial.

    Uses a two-ESP32 setup with ESP-NOW:
    - ESP32-S3 "receiver dongle" plugs into computer USB, receives ESP-NOW commands
    - ESP32 dev "encoder" has rotary encoder, sends commands via ESP-NOW

    The receiver forwards simple text commands over USB serial:
    - '+' or '+N': Volume up (N ticks)
    - '-' or '-N': Volume down (N ticks)
    - 'M': Mute toggle (encoder button press)
    """

    # Connection signals
    connectionStatusChanged = Signal(str)      # "Connected", "Disconnected", "Connecting", "Error"
    connectionDetailChanged = Signal(str)      # Human-readable status message
    availablePortsChanged = Signal(list)       # List of detected serial ports

    # Volume control signals (emitted when ESP32 sends commands)
    volumeChangeRequested = Signal(float)      # Relative volume change (delta, can be fractional)
    muteToggleRequested = Signal()             # Mute button pressed

    def __init__(self, settings_manager=None):
        super().__init__()
        self._settings_manager = settings_manager
        self._serial_connection = None
        self._connected = False
        self._connection_status = "Disconnected"
        self._connection_detail = "Not connected"
        self._available_ports = []
        self._esp32_s3_port = None  # Auto-detected ESP32-S3 port
        self._step_size = 1.0

        # Threading
        self._read_thread = None
        self._stop_thread = False
        self._lock = threading.Lock()

        # Auto-reconnect
        self._reconnect_timer = QTimer()
        self._reconnect_timer.setSingleShot(True)
        self._reconnect_timer.timeout.connect(self._attempt_reconnect)
        self._reconnect_delay = 3000  # 3 seconds
        self._reconnect_attempts = 0
        self._max_reconnect_attempts = 5

        # Port scanner timer (background scanning when disconnected)
        self._scan_timer = QTimer()
        self._scan_timer.setInterval(10000)  # 10 seconds
        self._scan_timer.timeout.connect(self._scan_for_devices)

        # Startup timer - delay initial connection attempt
        self._startup_timer = QTimer()
        self._startup_timer.setSingleShot(True)
        self._startup_timer.setInterval(3000)  # 3 second delay after app start
        self._startup_timer.timeout.connect(self._initial_connect)

        # Volume update rate limiting to prevent LED flickering
        self._pending_volume = None
        self._volume_send_timer = QTimer()
        self._volume_send_timer.setSingleShot(True)
        self._volume_send_timer.setInterval(50)  # 50ms debounce = max 20 updates/sec
        self._volume_send_timer.timeout.connect(self._send_pending_volume)

        # Keepalive timer - sends periodic volume updates to prevent ESP32 timeout
        self._keepalive_timer = QTimer()
        self._keepalive_timer.setInterval(2000)  # Every 2 seconds (ESP32 timeout is 5s)
        self._keepalive_timer.timeout.connect(self._send_keepalive)

        # Connect to settings if provided
        if self._settings_manager:
            self._connect_settings_signals()
            self._startup_timer.start()

    def connect_settings_manager(self, settings_manager):
        """Connect to settings manager after construction."""
        if self._settings_manager:
            return
        self._settings_manager = settings_manager
        self._connect_settings_signals()
        self._startup_timer.start()

    def _connect_settings_signals(self):
        """Wire up settings manager signals."""
        if not self._settings_manager:
            return
        self._settings_manager.esp32VolumePortChanged.connect(self._on_port_changed)
        self._settings_manager.esp32VolumeEnabledChanged.connect(self._on_enabled_changed)
        self._settings_manager.esp32VolumeStepSizeChanged.connect(self._on_step_size_changed)
        self._settings_manager.esp32AutoReconnectChanged.connect(self._on_auto_reconnect_changed)
        self._settings_manager.esp32LedSleepEnabledChanged.connect(self._on_led_sleep_changed)

        # Load initial step size
        self._step_size = self._settings_manager.esp32VolumeStepSize

    # ==================== Platform Detection ====================

    def _get_platform(self):
        """Detect the current platform."""
        if sys.platform.startswith('win'):
            return 'windows'
        elif sys.platform.startswith('darwin'):
            return 'macos'
        return 'linux'

    # ==================== Device Discovery ====================

    def _scan_for_devices(self):
        """Scan for available serial ports, prioritizing ESP32-S3 USB devices."""
        platform = self._get_platform()
        ports = []
        self._esp32_s3_port = None  # Track detected ESP32-S3 for auto-connect

        try:
            if platform == 'windows':
                for port in serial.tools.list_ports.comports():
                    desc = port.description.lower()
                    hwid = (port.hwid or '').lower()

                    # ESP32-S3 native USB appears as "USB Serial Device" or "USB JTAG/serial debug unit"
                    # VID:PID for ESP32-S3 is typically 303A:1001 (Espressif)
                    is_esp32_s3 = (
                        '303a' in hwid or  # Espressif VID
                        'jtag' in desc or
                        'esp32-s3' in desc or
                        'esp32s3' in desc
                    )

                    # Also look for common USB-serial chips used with ESP32
                    is_usb_serial = any(kw in desc for kw in [
                        'cp210', 'ch340', 'ch910', 'ftdi', 'usb serial',
                        'silicon labs', 'prolific'
                    ])

                    if is_esp32_s3:
                        ports.insert(0, port.device)  # Prioritize ESP32-S3
                        self._esp32_s3_port = port.device
                        logger.info(f"ESP32 volume: detected ESP32-S3 on {port.device}")
                    elif is_usb_serial or port.device.startswith('COM'):
                        ports.append(port.device)

            elif platform == 'macos':
                # ESP32-S3 USB on macOS
                for port in serial.tools.list_ports.comports():
                    desc = port.description.lower() if port.description else ''
                    hwid = (port.hwid or '').lower()

                    if '303a' in hwid or 'jtag' in desc:
                        ports.insert(0, port.device)
                        self._esp32_s3_port = port.device
                        logger.info(f"ESP32 volume: detected ESP32-S3 on {port.device}")
                        continue

                # USB serial devices
                ports.extend(glob.glob('/dev/tty.usbserial*'))
                ports.extend(glob.glob('/dev/cu.usbserial*'))
                ports.extend(glob.glob('/dev/tty.usbmodem*'))
                ports.extend(glob.glob('/dev/cu.usbmodem*'))
                ports.extend(glob.glob('/dev/tty.SLAB*'))
                ports.extend(glob.glob('/dev/cu.SLAB*'))
                ports.extend(glob.glob('/dev/tty.wchusbserial*'))
                ports.extend(glob.glob('/dev/cu.wchusbserial*'))

            else:  # Linux
                # ESP32-S3 USB on Linux
                for port in serial.tools.list_ports.comports():
                    hwid = (port.hwid or '').lower()

                    if '303a' in hwid:
                        ports.insert(0, port.device)
                        self._esp32_s3_port = port.device
                        logger.info(f"ESP32 volume: detected ESP32-S3 on {port.device}")
                        continue

                # USB serial devices
                ports.extend(glob.glob('/dev/ttyUSB*'))
                ports.extend(glob.glob('/dev/ttyACM*'))

            # Filter out OBD port to avoid conflicts
            if self._settings_manager:
                obd_port = self._settings_manager.obdBluetoothPort
                if obd_port:
                    ports = [p for p in ports if p != obd_port]

            ports = sorted(list(set(ports)))

            if ports != self._available_ports:
                self._available_ports = ports
                self.availablePortsChanged.emit(ports)
                logger.debug(f"ESP32 volume: discovered ports: {ports}")

        except Exception as e:
            logger.error(f"ESP32 volume: error scanning for devices: {e}")

        return ports

    # ==================== Settings Change Handlers ====================

    def _on_port_changed(self):
        """Handle port setting change."""
        if not self._settings_manager:
            return
        new_port = self._settings_manager.esp32VolumePort
        logger.info(f"ESP32 volume: port changed to {new_port}")
        # Reconnect if enabled
        if self._settings_manager.esp32VolumeEnabled and new_port:
            self.disconnect_device()
            self._reconnect_attempts = 0
            QTimer.singleShot(500, self.connect_device)

    def _on_enabled_changed(self):
        """Handle enabled setting change."""
        if not self._settings_manager:
            return
        enabled = self._settings_manager.esp32VolumeEnabled
        logger.info(f"ESP32 volume: enabled changed to {enabled}")
        if enabled:
            self.connect_device()
        else:
            self.disconnect_device()
            self._scan_timer.stop()

    def _on_step_size_changed(self):
        """Handle step size setting change."""
        if not self._settings_manager:
            return
        self._step_size = self._settings_manager.esp32VolumeStepSize
        logger.debug(f"ESP32 volume: step size changed to {self._step_size}")

    def _on_auto_reconnect_changed(self):
        """Handle auto-reconnect setting change."""
        if not self._settings_manager:
            return
        if not self._settings_manager.esp32AutoReconnect:
            self._reconnect_timer.stop()

    def _on_led_sleep_changed(self):
        """Handle LED sleep setting change."""
        if not self._settings_manager:
            return
        sleep_enabled = self._settings_manager.esp32LedSleepEnabled
        logger.info(f"ESP32 volume: LED sleep setting changed to {sleep_enabled}")

        if self._connected:
            if sleep_enabled:
                # Sleep enabled = stop keepalives (LEDs will turn off after timeout)
                self._keepalive_timer.stop()
                logger.info("ESP32 volume: keepalive timer stopped (sleep enabled)")
            else:
                # Sleep disabled = start keepalives (LEDs stay on)
                self._keepalive_timer.start()
                logger.info("ESP32 volume: keepalive timer started (sleep disabled)")

    # ==================== Connection Management ====================

    def _initial_connect(self):
        """Initial connection attempt on startup."""
        if not self._settings_manager:
            return

        # Scan for devices first (this also detects ESP32-S3)
        self._scan_for_devices()

        if self._settings_manager.esp32VolumeEnabled:
            port = self._settings_manager.esp32VolumePort

            # Auto-select ESP32-S3 if detected and no port configured
            if not port and self._esp32_s3_port:
                logger.info(f"ESP32 volume: auto-selecting ESP32-S3 on {self._esp32_s3_port}")
                self._settings_manager.save_esp32_volume_port(self._esp32_s3_port)
                port = self._esp32_s3_port
            # Fallback: try first available USB serial port
            elif not port and self._available_ports:
                obd_port = self._settings_manager.obdBluetoothPort
                for p in self._available_ports:
                    if p != obd_port:
                        logger.info(f"ESP32 volume: auto-selecting port {p}")
                        self._settings_manager.save_esp32_volume_port(p)
                        port = p
                        break

            if port:
                logger.info("ESP32 volume: starting initial connection...")
                self.connect_device()
            else:
                logger.info("ESP32 volume: enabled but no port configured")
                self._connection_detail = "No port configured - select one above"
                self.connectionDetailChanged.emit(self._connection_detail)
        else:
            logger.debug("ESP32 volume: disabled, skipping initial connection")

    @Slot()
    def connect_device(self):
        """Connect to the ESP32 device."""
        logger.info("ESP32 volume: connect_device() called")

        with self._lock:
            if self._connected:
                logger.debug("ESP32 volume: already connected")
                return

        if not self._settings_manager:
            logger.warning("ESP32 volume: no settings manager!")
            return

        logger.info(f"ESP32 volume: enabled={self._settings_manager.esp32VolumeEnabled}, port={self._settings_manager.esp32VolumePort}")

        if not self._settings_manager.esp32VolumeEnabled:
            logger.debug("ESP32 volume: disabled, not connecting")
            return

        port = self._settings_manager.esp32VolumePort
        if not port:
            self._connection_status = "Not Configured"
            self._connection_detail = "No port configured"
            self.connectionStatusChanged.emit(self._connection_status)
            self.connectionDetailChanged.emit(self._connection_detail)
            # Start scanning for devices
            self._scan_timer.start()
            return

        self._connection_status = "Connecting"
        self._connection_detail = f"Connecting to {port}..."
        self.connectionStatusChanged.emit(self._connection_status)
        self.connectionDetailChanged.emit(self._connection_detail)

        try:
            self._serial_connection = serial.Serial(
                port=port,
                baudrate=115200,
                timeout=1,
                write_timeout=1
            )

            with self._lock:
                self._connected = True

            self._connection_status = "Connected"
            self._connection_detail = f"Connected to ESP32 on {port}"
            self.connectionStatusChanged.emit(self._connection_status)
            self.connectionDetailChanged.emit(self._connection_detail)
            logger.info(f"ESP32 volume: connected to {port}")

            # Reset reconnect attempts on successful connection
            self._reconnect_attempts = 0

            # Start read thread
            self._stop_thread = False
            self._read_thread = threading.Thread(target=self._read_loop, daemon=True)
            self._read_thread.start()

            # Start keepalive timer to prevent ESP32 LED timeout (only if sleep is disabled)
            if self._settings_manager and not self._settings_manager.esp32LedSleepEnabled:
                self._keepalive_timer.start()
                logger.info("ESP32 volume: keepalive timer started (sleep disabled)")
            else:
                logger.info("ESP32 volume: keepalive timer NOT started (sleep enabled)")

            # Stop scanning while connected
            self._scan_timer.stop()

        except serial.SerialException as e:
            logger.error(f"ESP32 volume: connection failed: {e}")
            with self._lock:
                self._connected = False
            self._connection_status = "Error"
            self._connection_detail = f"Connection failed: {str(e)}"
            self.connectionStatusChanged.emit(self._connection_status)
            self.connectionDetailChanged.emit(self._connection_detail)
            self._schedule_reconnect()

    @Slot()
    def disconnect_device(self):
        """Disconnect from the ESP32 device."""
        logger.info("ESP32 volume: disconnecting...")

        # Stop reconnect timer and keepalive timer
        self._reconnect_timer.stop()
        self._keepalive_timer.stop()

        # Signal thread to stop
        self._stop_thread = True

        # Wait for read thread to finish
        if self._read_thread and self._read_thread.is_alive():
            self._read_thread.join(timeout=2.0)
        self._read_thread = None

        # Close serial connection
        if self._serial_connection:
            try:
                self._serial_connection.close()
            except Exception as e:
                logger.error(f"ESP32 volume: error closing serial: {e}")
            self._serial_connection = None

        with self._lock:
            self._connected = False

        self._connection_status = "Disconnected"
        self._connection_detail = "Disconnected from ESP32"
        self.connectionStatusChanged.emit(self._connection_status)
        self.connectionDetailChanged.emit(self._connection_detail)

    def _schedule_reconnect(self):
        """Schedule an automatic reconnection attempt."""
        if not self._settings_manager:
            return

        if not self._settings_manager.esp32AutoReconnect:
            logger.debug("ESP32 volume: auto-reconnect disabled")
            self._scan_timer.start()
            return

        if self._reconnect_attempts >= self._max_reconnect_attempts:
            logger.warning(f"ESP32 volume: max reconnect attempts ({self._max_reconnect_attempts}) reached")
            self._connection_detail = "Max retries reached. Check connection."
            self.connectionDetailChanged.emit(self._connection_detail)
            self._scan_timer.start()
            return

        self._reconnect_attempts += 1
        delay = min(10000, self._reconnect_delay * self._reconnect_attempts)  # Exponential backoff, max 10s

        logger.info(f"ESP32 volume: scheduling reconnect in {delay}ms (attempt {self._reconnect_attempts}/{self._max_reconnect_attempts})")
        self._connection_detail = f"Reconnecting in {delay // 1000}s... ({self._reconnect_attempts}/{self._max_reconnect_attempts})"
        self.connectionDetailChanged.emit(self._connection_detail)

        self._reconnect_timer.setInterval(delay)
        self._reconnect_timer.start()

    def _attempt_reconnect(self):
        """Attempt to reconnect to the device."""
        if self._connected:
            return
        logger.info("ESP32 volume: attempting reconnect...")
        self.connect_device()

    def _handle_disconnect(self):
        """Handle unexpected disconnect (called from read thread via signal)."""
        with self._lock:
            if not self._connected:
                return
            self._connected = False

        self._connection_status = "Disconnected"
        self._connection_detail = "Connection lost"
        self.connectionStatusChanged.emit(self._connection_status)
        self.connectionDetailChanged.emit(self._connection_detail)

        logger.warning("ESP32 volume: connection lost, scheduling reconnect")
        self._schedule_reconnect()

    # ==================== Serial Read Loop ====================

    def _read_loop(self):
        """Background thread to read serial data from ESP32."""
        buffer = ""

        while not self._stop_thread and self._serial_connection:
            try:
                # Block in readline() (1 s port timeout, GIL released) instead of
                # polling in_waiting at 100 Hz for a knob that sends a few bytes
                # a second. A timeout returns b"" and re-checks _stop_thread.
                data = self._serial_connection.readline()
                if not data:
                    continue
                buffer += data.decode('utf-8', errors='ignore')

                # Process complete lines (readline may return a partial line on timeout)
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    line = line.strip()
                    if line:
                        self._process_command(line)

            except serial.SerialException as e:
                logger.error(f"ESP32 volume: serial read error: {e}")
                # Schedule reconnect on main thread
                QTimer.singleShot(0, self._handle_disconnect)
                break
            except Exception as e:
                logger.error(f"ESP32 volume: read loop error: {e}")
                time.sleep(0.1)  # Brief pause before retrying

    def _process_command(self, command):
        """Process a command received from ESP32."""
        command = command.strip().upper()
        logger.debug(f"ESP32 volume: received command: {command}")

        if command == '+':
            # Single tick clockwise
            self.volumeChangeRequested.emit(self._step_size)
        elif command == '-':
            # Single tick counter-clockwise
            self.volumeChangeRequested.emit(-self._step_size)
        elif command.startswith('+') and len(command) > 1:
            # Multiple ticks clockwise: +3
            try:
                ticks = int(command[1:])
                self.volumeChangeRequested.emit(self._step_size * ticks)
            except ValueError:
                logger.warning(f"ESP32 volume: invalid command format: {command}")
        elif command.startswith('-') and len(command) > 1:
            # Multiple ticks counter-clockwise: -2
            try:
                ticks = int(command[1:])
                self.volumeChangeRequested.emit(-self._step_size * ticks)
            except ValueError:
                logger.warning(f"ESP32 volume: invalid command format: {command}")
        elif command == 'M':
            # Mute toggle (encoder button press)
            self.muteToggleRequested.emit()
        elif command == 'P':
            # Play/pause (optional - double-click)
            logger.debug("ESP32 volume: play/pause command (not implemented)")
        else:
            logger.debug(f"ESP32 volume: unknown command: {command}")

    # ==================== Public Slots for QML ====================

    @Slot(result=list)
    def get_available_ports(self):
        """Get list of available serial ports."""
        self._scan_for_devices()
        return self._available_ports

    @Slot(result=str)
    def get_ports_with_descriptions(self):
        """Get JSON array of ports with descriptions for QML dropdown."""
        import json
        ports_info = []
        obd_port = self._settings_manager.obdBluetoothPort if self._settings_manager else ""

        for port in serial.tools.list_ports.comports():
            # Skip OBD port
            if port.device == obd_port:
                continue

            desc = port.description or "Unknown Device"
            hwid = (port.hwid or '').lower()

            # Identify ESP32-S3
            is_esp32_s3 = '303a' in hwid or 'jtag' in desc.lower() or 'esp32-s3' in desc.lower()

            # Build display label
            if is_esp32_s3:
                label = f"{port.device} - ESP32-S3 (Recommended)"
            else:
                label = f"{port.device} - {desc}"

            ports_info.append({
                "port": port.device,
                "label": label,
                "description": desc,
                "isEsp32S3": is_esp32_s3
            })

        # Sort: ESP32-S3 first, then by port name
        ports_info.sort(key=lambda x: (not x["isEsp32S3"], x["port"]))

        return json.dumps(ports_info)

    @Slot(result=bool)
    def is_connected(self):
        """Return connection status."""
        return self._connected

    @Slot(result=str)
    def get_connection_status(self):
        """Get human-readable connection status."""
        return self._connection_status

    @Slot(result=str)
    def get_connection_detail(self):
        """Get detailed connection status message."""
        return self._connection_detail

    @Slot()
    def refresh_ports(self):
        """Manually trigger a port scan."""
        self._scan_for_devices()

    @Slot()
    def reset_reconnect_attempts(self):
        """Reset the reconnect attempt counter."""
        self._reconnect_attempts = 0

    # ==================== Properties ====================

    @Property(bool, notify=connectionStatusChanged)
    def connected(self):
        """Property for connection status."""
        return self._connected

    @Property(str, notify=connectionStatusChanged)
    def connectionStatus(self):
        """Property for connection status string."""
        return self._connection_status

    @Property(str, notify=connectionDetailChanged)
    def connectionDetail(self):
        """Property for connection detail string."""
        return self._connection_detail

    @Property(list, notify=availablePortsChanged)
    def availablePorts(self):
        """Property for available ports list."""
        return self._available_ports

    # ==================== Send Commands to ESP32 ====================

    def send_volume_update(self, volume: int):
        """Send current volume level to ESP32 for LED display (rate-limited)."""
        if not self._connected or not self._serial_connection:
            return

        # Store the pending volume and restart the debounce timer
        # This ensures we send the most recent value after rapid changes settle
        self._pending_volume = volume

        # If timer is not running, start it; otherwise it will restart on next call
        if not self._volume_send_timer.isActive():
            self._volume_send_timer.start()

    def _send_pending_volume(self):
        """Actually send the pending volume update (called after debounce)."""
        if self._pending_volume is None:
            return

        if not self._connected or not self._serial_connection:
            self._pending_volume = None
            return

        try:
            # Send volume command: V## (0-100)
            cmd = f"V{self._pending_volume}\n"
            logger.info(f"ESP32 volume: sending volume command: {cmd.strip()}")
            self._serial_connection.write(cmd.encode('utf-8'))
        except serial.SerialException as e:
            logger.error(f"ESP32 volume: failed to send volume: {e}")
        except Exception as e:
            logger.error(f"ESP32 volume: error sending volume: {e}")
        finally:
            self._pending_volume = None

    def _send_keepalive(self):
        """Send periodic keepalive to prevent ESP32 timeout (LEDs turning off)."""
        if not self._connected or not self._serial_connection:
            return

        try:
            # Just send current volume as keepalive - ESP32 uses any data to reset timeout
            if self._settings_manager:
                volume = self._settings_manager.currentVolume
                cmd = f"V{volume}\n"
                self._serial_connection.write(cmd.encode('utf-8'))
                # Don't log keepalives to avoid spam
        except serial.SerialException as e:
            logger.error(f"ESP32 volume: keepalive failed: {e}")
            self._handle_disconnect()
        except Exception as e:
            logger.error(f"ESP32 volume: keepalive error: {e}")

    def send_mute_state(self, muted: bool):
        """Send mute state to ESP32 for LED display."""
        if not self._connected or not self._serial_connection:
            logger.info(f"ESP32 volume: send_mute_state({muted}) - not connected, skipping")
            return

        try:
            # Send mute command: M0 (unmuted) or M1 (muted)
            cmd = f"M{1 if muted else 0}\n"
            logger.info(f"ESP32 volume: sending mute command: {cmd.strip()}")
            self._serial_connection.write(cmd.encode('utf-8'))
        except serial.SerialException as e:
            logger.error(f"ESP32 volume: failed to send mute state: {e}")
        except Exception as e:
            logger.error(f"ESP32 volume: error sending mute state: {e}")

    def send_theme_color(self, r: int, g: int, b: int):
        """Send theme color to ESP32 for LED display."""
        if not self._connected or not self._serial_connection:
            logger.info(f"ESP32 volume: send_theme_color({r},{g},{b}) - not connected, skipping")
            return

        try:
            # Send color command: C###,###,###
            cmd = f"C{r},{g},{b}\n"
            logger.info(f"ESP32 volume: sending color command: {cmd.strip()}")
            self._serial_connection.write(cmd.encode('utf-8'))
            logger.debug(f"ESP32 volume: sent theme color: RGB({r},{g},{b})")
        except serial.SerialException as e:
            logger.error(f"ESP32 volume: failed to send theme color: {e}")
        except Exception as e:
            logger.error(f"ESP32 volume: error sending theme color: {e}")

    # ==================== Cleanup ====================

    @Slot()
    def cleanup(self):
        """Cleanup on application exit."""
        logger.info("ESP32 volume: cleaning up...")
        self._reconnect_timer.stop()
        self._scan_timer.stop()
        self._startup_timer.stop()
        self._volume_send_timer.stop()
        self._keepalive_timer.stop()
        self.disconnect_device()
