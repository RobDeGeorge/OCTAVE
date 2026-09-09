from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, QThread
import obd
from obd import OBDStatus
import time
import os
import re
import sys
import shutil
import subprocess
import threading
import glob

from backend.logging_config import get_logger
logger = get_logger(__name__)


class OBDConnectionWorker(QObject):
    """Worker that runs OBD connection in a separate thread to avoid blocking UI"""

    # Signals to communicate back to main thread
    connectionComplete = Signal(object, object)  # (connection, status)
    connectionProgress = Signal(int, str)  # (progress, message)
    connectionError = Signal(str)

    def __init__(self):
        super().__init__()
        self._port = None
        self._fast_mode = True
        self._timeout = 5  # Reduced from 10s for faster connection
        self._cached_protocol = None

    def set_params(self, port, fast_mode, timeout, cached_protocol=None):
        self._port = port
        self._fast_mode = fast_mode
        self._timeout = timeout
        self._cached_protocol = cached_protocol

    def do_connect(self):
        """Perform the actual OBD connection - runs in worker thread"""
        try:
            if self._cached_protocol:
                self.connectionProgress.emit(20, f"Connecting with cached protocol...")
                logger.info(f"[OBD] Attempting connection with cached protocol: {self._cached_protocol}")
            else:
                self.connectionProgress.emit(20, "Initializing OBD adapter...")

            # Create the connection (this is the blocking call)
            # SPEED OPTIMIZATIONS:
            # - delay_cmds=0: No delay between polling cycles (default is 0.25s!)
            # - protocol: Use cached protocol to skip auto-detection (saves 1-3 sec)
            connection = obd.Async(
                portstr=self._port,
                fast=self._fast_mode,
                timeout=self._timeout,
                delay_cmds=0,  # MAXIMUM SPEED - change to 0.02 if issues arise
                protocol=self._cached_protocol if self._cached_protocol else None
            )

            self.connectionProgress.emit(80, "Checking connection status...")
            status = connection.status()

            self.connectionComplete.emit(connection, status)

        except Exception as e:
            self.connectionError.emit(str(e))


class OBDManager(QObject):
    # Signals for OBD parameters - Original 18
    coolantTempChanged = Signal(float)
    voltageChanged = Signal(float)
    engineLoadChanged = Signal(float)
    throttlePositionChanged = Signal(float)
    intakeAirTempChanged = Signal(float)
    timingAdvanceChanged = Signal(float)
    massAirFlowChanged = Signal(float)
    speedMPHChanged = Signal(float)
    rpmChanged = Signal(float)
    airFuelRatioChanged = Signal(float)
    fuelLevelChanged = Signal(float)
    intakeManifoldPressureChanged = Signal(float)
    shortTermFuelTrimChanged = Signal(float)
    longTermFuelTrimChanged = Signal(float)
    oxygenSensorVoltageChanged = Signal(float)
    fuelPressureChanged = Signal(float)
    engineOilTempChanged = Signal(float)
    ignitionTimingChanged = Signal(float)

    # Signals for additional OBD parameters - Batch 1
    runTimeChanged = Signal(float)
    distanceWithMILChanged = Signal(float)
    fuelRailPressureChanged = Signal(float)
    fuelRailPressureDirectChanged = Signal(float)
    barometricPressureChanged = Signal(float)
    ambientAirTempChanged = Signal(float)
    relativeThrottlePosChanged = Signal(float)
    absoluteThrottlePosBChanged = Signal(float)
    acceleratorPosChanged = Signal(float)
    catalystTempB1S1Changed = Signal(float)
    catalystTempB1S2Changed = Signal(float)
    evapVaporPressureChanged = Signal(float)
    shortFuelTrim2Changed = Signal(float)
    longFuelTrim2Changed = Signal(float)
    o2SensorB1S2Changed = Signal(float)
    o2SensorB2S1Changed = Signal(float)
    o2SensorB2S2Changed = Signal(float)
    distanceSinceCodesCleared = Signal(float)
    warmupsSinceCodesCleared = Signal(float)
    absoluteLoadChanged = Signal(float)
    commandedEGRChanged = Signal(float)
    egrErrorChanged = Signal(float)
    ethanoPercentChanged = Signal(float)

    # Signals for additional OBD parameters - Batch 2 (remaining PIDs)
    # Additional O2 sensors
    o2SensorB1S3Changed = Signal(float)
    o2SensorB1S4Changed = Signal(float)
    o2SensorB2S3Changed = Signal(float)
    o2SensorB2S4Changed = Signal(float)
    # Wide-range O2 sensors voltage
    o2S1WRVoltageChanged = Signal(float)
    o2S2WRVoltageChanged = Signal(float)
    o2S3WRVoltageChanged = Signal(float)
    o2S4WRVoltageChanged = Signal(float)
    o2S5WRVoltageChanged = Signal(float)
    o2S6WRVoltageChanged = Signal(float)
    o2S7WRVoltageChanged = Signal(float)
    o2S8WRVoltageChanged = Signal(float)
    # Wide-range O2 sensors current
    o2S1WRCurrentChanged = Signal(float)
    o2S2WRCurrentChanged = Signal(float)
    o2S3WRCurrentChanged = Signal(float)
    o2S4WRCurrentChanged = Signal(float)
    o2S5WRCurrentChanged = Signal(float)
    o2S6WRCurrentChanged = Signal(float)
    o2S7WRCurrentChanged = Signal(float)
    o2S8WRCurrentChanged = Signal(float)
    # Bank 2 catalyst temps
    catalystTempB2S1Changed = Signal(float)
    catalystTempB2S2Changed = Signal(float)
    # Additional throttle/accelerator
    throttlePosCChanged = Signal(float)
    acceleratorPosEChanged = Signal(float)
    acceleratorPosFChanged = Signal(float)
    throttleActuatorChanged = Signal(float)
    # Fuel system
    evaporativePurgeChanged = Signal(float)
    fuelRailPressureAbsChanged = Signal(float)
    fuelInjectTimingChanged = Signal(float)
    fuelRateChanged = Signal(float)
    # Time-based
    runTimeMILChanged = Signal(float)
    timeSinceDTCClearedChanged = Signal(float)
    # Other
    maxMAFChanged = Signal(float)
    fuelTypeChanged = Signal(float)
    evapVaporPressureAbsChanged = Signal(float)
    evapVaporPressureAltChanged = Signal(float)
    shortO2TrimB1Changed = Signal(float)
    longO2TrimB1Changed = Signal(float)
    shortO2TrimB2Changed = Signal(float)
    longO2TrimB2Changed = Signal(float)
    relativeAccelPosChanged = Signal(float)
    hybridBatteryRemainingChanged = Signal(float)
    elmVoltageChanged = Signal(float)

    # Connection status signals
    connectionStatusChanged = Signal(str)
    connectionStatusDetailChanged = Signal(str)
    connectionProgressChanged = Signal(int)
    devicePresenceChanged = Signal(bool)
    availablePortsChanged = Signal(list)  # Signal for discovered ports
    # Emitted alongside availablePortsChanged but with the richer
    # {name, identifier, kind} shape the unified QML binds to.
    availableAdaptersChanged = Signal(list)
    # Live connection-log line — same surface the C++ side emits so the QML
    # connection-log card binds to either backend without branching.
    connectionLogLineAppended = Signal(str)
    supportedCommandsChanged = Signal(list)  # Signal for vehicle-supported commands
    scanProgressChanged = Signal(int, str)  # progress (0-100), message
    scanCompleteChanged = Signal(list)  # list of supported command names
    scanOutputChanged = Signal(str)  # Terminal-style output for vehicle scanning

    # Diagnostic signals
    dtcCodesChanged = Signal(list)  # List of DTC tuples [(code, description), ...]
    dtcCountChanged = Signal(int)  # Number of DTCs
    milStatusChanged = Signal(bool)  # Check Engine Light status
    dtcClearResult = Signal(bool, str)  # success, message
    freezeFrameChanged = Signal(list)  # Freeze frame DTCs

    # Internal signals for thread-safe emission from background threads
    _emitDtcCodes = Signal(list)
    _emitDtcCount = Signal(int)
    _emitMilStatus = Signal(bool)
    _emitDtcClearResult = Signal(bool, str)
    _emitFreezeFrame = Signal(list)

    def __init__(self, settings_manager=None):
        super().__init__()
        self._connection = None
        self._connected = False
        self._settings_manager = settings_manager

        # Thread safety lock
        self._lock = threading.Lock()

        # OBD parameter values - Original 18
        self._coolant_temp = 0.0
        self._voltage = 0.0
        self._engine_load = 0.0
        self._throttle_pos = 0.0
        self._intake_temp = 0.0
        self._timing_advance = 0.0
        self._mass_airflow = 0.0
        self._speed_mph = 0.0
        self._rpm = 0.0
        self._air_fuel_ratio = 0.0
        self._fuel_level = 0.0
        self._intake_pressure = 0.0
        self._short_term_fuel_trim = 0.0
        self._long_term_fuel_trim = 0.0
        self._o2_sensor_voltage = 0.0
        self._fuel_pressure = 0.0
        self._oil_temp = 0.0
        self._ignition_timing = 0.0

        # Additional OBD parameter values
        self._run_time = 0.0
        self._distance_with_mil = 0.0
        self._fuel_rail_pressure = 0.0
        self._fuel_rail_pressure_direct = 0.0
        self._barometric_pressure = 0.0
        self._ambient_air_temp = 0.0
        self._relative_throttle_pos = 0.0
        self._absolute_throttle_pos_b = 0.0
        self._accelerator_pos = 0.0
        self._catalyst_temp_b1s1 = 0.0
        self._catalyst_temp_b1s2 = 0.0
        self._evap_vapor_pressure = 0.0
        self._short_fuel_trim_2 = 0.0
        self._long_fuel_trim_2 = 0.0
        self._o2_sensor_b1s2 = 0.0
        self._o2_sensor_b2s1 = 0.0
        self._o2_sensor_b2s2 = 0.0
        self._distance_since_codes_cleared = 0.0
        self._warmups_since_codes_cleared = 0.0
        self._absolute_load = 0.0
        self._commanded_egr = 0.0
        self._egr_error = 0.0
        self._ethanol_percent = 0.0

        # Batch 2 parameter values - Additional O2 sensors
        self._o2_sensor_b1s3 = 0.0
        self._o2_sensor_b1s4 = 0.0
        self._o2_sensor_b2s3 = 0.0
        self._o2_sensor_b2s4 = 0.0
        # Wide-range O2 sensors voltage
        self._o2_s1_wr_voltage = 0.0
        self._o2_s2_wr_voltage = 0.0
        self._o2_s3_wr_voltage = 0.0
        self._o2_s4_wr_voltage = 0.0
        self._o2_s5_wr_voltage = 0.0
        self._o2_s6_wr_voltage = 0.0
        self._o2_s7_wr_voltage = 0.0
        self._o2_s8_wr_voltage = 0.0
        # Wide-range O2 sensors current
        self._o2_s1_wr_current = 0.0
        self._o2_s2_wr_current = 0.0
        self._o2_s3_wr_current = 0.0
        self._o2_s4_wr_current = 0.0
        self._o2_s5_wr_current = 0.0
        self._o2_s6_wr_current = 0.0
        self._o2_s7_wr_current = 0.0
        self._o2_s8_wr_current = 0.0
        # Bank 2 catalyst temps
        self._catalyst_temp_b2s1 = 0.0
        self._catalyst_temp_b2s2 = 0.0
        # Additional throttle/accelerator
        self._throttle_pos_c = 0.0
        self._accelerator_pos_e = 0.0
        self._accelerator_pos_f = 0.0
        self._throttle_actuator = 0.0
        # Fuel system
        self._evaporative_purge = 0.0
        self._fuel_rail_pressure_abs = 0.0
        self._fuel_inject_timing = 0.0
        self._fuel_rate = 0.0
        # Time-based
        self._run_time_mil = 0.0
        self._time_since_dtc_cleared = 0.0
        # Other
        self._max_maf = 0.0
        self._fuel_type = 0.0
        self._evap_vapor_pressure_abs = 0.0
        self._evap_vapor_pressure_alt = 0.0
        self._short_o2_trim_b1 = 0.0
        self._long_o2_trim_b1 = 0.0
        self._short_o2_trim_b2 = 0.0
        self._long_o2_trim_b2 = 0.0
        self._relative_accel_pos = 0.0
        self._hybrid_battery_remaining = 0.0
        self._elm_voltage = 0.0

        # Supported commands detected from vehicle
        self._supported_commands = []
        self._is_scanning = False

        # Diagnostic data
        self._dtc_codes = []  # List of (code, description) tuples
        self._dtc_count = 0
        self._mil_status = False  # Check Engine Light
        self._freeze_frame_dtcs = []
        self._diagnostic_mode = False  # True when in diagnostic menu (async paused)
        self._diagnostic_sync_conn = None  # Persistent sync connection for diagnostic mode
        self._diagnostic_mode_lock = threading.Lock()  # Prevent race conditions on rapid menu switching
        self._diagnostic_mode_transitioning = False  # True while entering/exiting diagnostic mode
        self._reconnecting_after_diagnostic = False  # True when reconnecting after exiting diagnostic mode

        # Connection management
        self._connection_attempts = 0
        self._connection_status = "Not Connected"
        self._connection_detail = "Waiting for startup..."
        self._connection_progress = 0
        # Keep the Q_PROPERTY-style readers (connectionDetail / connectionProgress)
        # in sync with whatever was last emitted — mirrors the C++ constructor hook.
        self.connectionStatusDetailChanged.connect(self._cache_connection_detail)
        self.connectionProgressChanged.connect(self._cache_connection_progress)
        self._is_connecting = False
        self._last_reconnect_time = 0
        self._last_successful_protocol = None  # Cache last working protocol for faster reconnects

        # Auto-reconnect settings (loaded from settings_manager)
        self._auto_reconnect_delay = 5.0  # seconds
        self._force_stop_reconnect = False  # Used to stop reconnects on close()

        # Connection timeout (configurable) - reduced from 10s for faster connection
        self._connection_timeout = 5  # seconds

        # Connection monitor thread
        self._monitor_thread = None
        self._stop_monitor = False

        # Data watchdog - detect stale connections where no data is being received
        self._last_data_received = 0  # timestamp of last data callback
        self._data_watchdog_timeout = 30.0  # seconds without data before reconnecting
        self._has_active_watchers = False  # Track if any parameters are being watched
        self._data_watchdog_timer = QTimer()
        self._data_watchdog_timer.setInterval(5000)  # Check every 5 seconds
        self._data_watchdog_timer.timeout.connect(self._check_data_watchdog)

        # Worker thread for non-blocking connections
        self._worker_thread = None
        self._worker = None

        # Device discovery
        self._available_ports = []
        self._last_device_scan = 0
        self._device_scan_interval = 10.0  # Scan for devices every 10 seconds when not connected

        # Background device scanner timer
        self._device_scanner_timer = QTimer()
        self._device_scanner_timer.setInterval(10000)  # 10 seconds
        self._device_scanner_timer.timeout.connect(self._scan_for_devices)

        # Debounce timer for settings changes - only reconnect if port changes
        self._port_change_timer = QTimer()
        self._port_change_timer.setSingleShot(True)
        self._port_change_timer.setInterval(1000)  # 1 second debounce for port changes
        self._port_change_timer.timeout.connect(self._on_port_changed)

        # Pending port change
        self._pending_port = None

        # Startup timer - defer connection to not block init
        self._startup_timer = QTimer()
        self._startup_timer.setSingleShot(True)
        self._startup_timer.setInterval(50)  # Minimal delay - just defer from constructor
        self._startup_timer.timeout.connect(self._initial_connect)
        self._startup_timer.start()

        # Connect internal signals to public signals for thread-safe emission
        # This allows background threads to emit via internal signals which are then
        # properly forwarded to QML on the main thread
        self._emitDtcCodes.connect(self._forwardDtcCodes)
        self._emitDtcCount.connect(self._forwardDtcCount)
        self._emitMilStatus.connect(self._forwardMilStatus)
        self._emitDtcClearResult.connect(self._forwardDtcClearResult)
        self._emitFreezeFrame.connect(self._forwardFreezeFrame)

        # Connect to settings changes
        if self._settings_manager:
            # Port changes trigger reconnect (debounced)
            self._settings_manager.obdBluetoothPortChanged.connect(self._schedule_port_change)
            # Fast mode changes are applied on next connection, no immediate reconnect needed
            # Parameter changes just refresh watchers, don't need full reconnect
            self._settings_manager.obdParametersChanged.connect(self._refresh_watchers)

    def _get_platform(self):
        """Detect the current platform"""
        if sys.platform.startswith('win'):
            return 'windows'
        elif sys.platform.startswith('darwin'):
            return 'macos'
        else:
            return 'linux'

    def _get_max_reconnect_attempts(self):
        """Get max reconnect attempts from settings (0 = disabled)"""
        if self._settings_manager:
            return self._settings_manager.obdAutoReconnectAttempts
        return 0  # Default to disabled

    def _is_auto_reconnect_enabled(self):
        """Check if auto-reconnect is enabled based on settings"""
        return self._get_max_reconnect_attempts() > 0

    def _scan_for_devices(self):
        """Scan for available OBD/serial ports based on platform"""
        platform = self._get_platform()
        ports = []

        try:
            if platform == 'windows':
                # Windows: Check COM ports
                import serial.tools.list_ports
                for port in serial.tools.list_ports.comports():
                    # Look for Bluetooth or OBD-related devices
                    desc_lower = port.description.lower()
                    if any(keyword in desc_lower for keyword in ['bluetooth', 'obd', 'elm', 'serial', 'usb']):
                        ports.append(port.device)
                    elif port.device.startswith('COM'):
                        # Include all COM ports as potential candidates
                        ports.append(port.device)

            elif platform == 'macos':
                # macOS: Check /dev/tty.* and /dev/cu.* devices
                ports.extend(glob.glob('/dev/tty.OBD*'))
                ports.extend(glob.glob('/dev/tty.Bluetooth*'))
                ports.extend(glob.glob('/dev/cu.OBD*'))
                ports.extend(glob.glob('/dev/cu.Bluetooth*'))
                ports.extend(glob.glob('/dev/tty.usbserial*'))
                ports.extend(glob.glob('/dev/cu.usbserial*'))
                # Also check for ELM327 devices
                ports.extend(glob.glob('/dev/tty.*ELM*'))
                ports.extend(glob.glob('/dev/cu.*ELM*'))

            else:
                # Linux: Check rfcomm devices and USB serial
                ports.extend(glob.glob('/dev/rfcomm*'))
                ports.extend(glob.glob('/dev/ttyUSB*'))
                ports.extend(glob.glob('/dev/ttyACM*'))
                # Note: /dev/ttyS* (hardware serial) excluded — almost never OBD devices
                # and produces many false positives on Linux

            # Remove duplicates and sort
            ports = sorted(list(set(ports)))

            # Update available ports if changed
            if ports != self._available_ports:
                self._available_ports = ports
                self.availablePortsChanged.emit(ports)
                self.availableAdaptersChanged.emit(self._build_adapter_list(ports))
                logger.info(f"[OBD] Discovered ports: {ports}")

                # If we found a new port and we're not connected, try to connect
                if ports and not self._connected and not self._is_connecting:
                    configured_port = self._get_configured_port()
                    if configured_port in ports:
                        logger.info(f"[OBD] Configured port {configured_port} is now available, attempting connection...")
                        self._connection_attempts = 0  # Reset attempts when device appears
                        self._start_connection()
                    elif ports:
                        # If configured port not found but we have other ports, notify user
                        self.connectionStatusDetailChanged.emit(f"Device at {configured_port} not found. Available: {', '.join(ports)}")

        except Exception as e:
            logger.error(f"[OBD] Error scanning for devices: {e}")

        self._last_device_scan = time.time()

    def _get_configured_port(self):
        """Get the configured port from settings, with platform-appropriate default"""
        if self._settings_manager:
            port = self._settings_manager.obdBluetoothPort
            if port:
                return port

        # Platform-specific defaults
        platform = self._get_platform()
        if platform == 'windows':
            return 'COM3'  # Common default for Bluetooth serial on Windows
        elif platform == 'macos':
            return '/dev/tty.OBD'
        else:
            return '/dev/rfcomm0'

    def _check_port_exists(self, port):
        """Check if a port exists, cross-platform"""
        platform = self._get_platform()

        if platform == 'windows':
            # On Windows, COM ports don't show up as files
            # We need to try to open them or check via serial.tools
            try:
                import serial.tools.list_ports
                available = [p.device for p in serial.tools.list_ports.comports()]
                return port in available
            except (ImportError, OSError) as e:
                # Fallback: assume port might be available
                logger.info(f"[OBD] Port check fallback: {e}")
                return True
        else:
            # On Unix-like systems, check if device file exists
            return os.path.exists(port)

    def _schedule_port_change(self):
        """Schedule a port change with debouncing"""
        new_port = self._settings_manager.obdBluetoothPort if self._settings_manager else None
        if new_port and new_port != self._pending_port:
            self._pending_port = new_port
            self._port_change_timer.start()
            logger.info(f"[OBD] Port change scheduled: {new_port}")

    def _on_port_changed(self):
        """Handle port change after debounce"""
        if self._pending_port:
            logger.info(f"[OBD] Port changed to: {self._pending_port}")
            self._connection_attempts = 0  # Reset on port change
            self.reconnect()
            self._pending_port = None

    def _initial_connect(self):
        """Initial connection attempt on startup"""
        logger.info("[OBD] Starting initial connection...")

        # First scan for devices
        self._scan_for_devices()

        # Start the background device scanner
        self._device_scanner_timer.start()

        # Attempt connection
        self._start_connection()

    def _start_connection(self):
        """Start a non-blocking connection attempt"""
        with self._lock:
            if self._is_connecting:
                logger.info("[OBD] Already connecting - skipping")
                return
            self._is_connecting = True

        self._connection_attempts += 1
        logger.info(f"[OBD] Starting connection attempt #{self._connection_attempts}")

        self.connectionStatusChanged.emit("Connecting")
        self.connectionStatusDetailChanged.emit(f"Attempt {self._connection_attempts}...")
        self.connectionProgressChanged.emit(10)

        # Get settings
        port = self._get_configured_port()
        fast_mode = True
        if self._settings_manager:
            fast_mode = self._settings_manager.obdFastMode

        # Check if device exists
        if not self._check_port_exists(port):
            self._connected = False
            self.connectionStatusChanged.emit("Device Not Found")
            self.connectionStatusDetailChanged.emit(f"Port {port} not available")
            self.connectionProgressChanged.emit(0)
            self.devicePresenceChanged.emit(False)
            logger.info(f"[OBD] Port {port} not found")
            with self._lock:
                self._is_connecting = False
            self._schedule_auto_reconnect()
            return

        self.devicePresenceChanged.emit(True)
        self.connectionProgressChanged.emit(15)
        self.connectionStatusDetailChanged.emit(f"Found {port}, connecting...")

        # Clean up previous thread if it exists
        if self._worker_thread is not None and self._worker_thread.isRunning():
            self._worker_thread.quit()
            self._worker_thread.wait(1000)

        # Create worker and thread for non-blocking connection using Qt threading
        self._worker = OBDConnectionWorker()
        self._worker.set_params(port, fast_mode, self._connection_timeout, self._last_successful_protocol)

        self._worker_thread = QThread()
        self._worker.moveToThread(self._worker_thread)

        # Connect signals
        self._worker.connectionProgress.connect(self._on_connection_progress)
        self._worker.connectionComplete.connect(self._on_connection_complete)
        self._worker.connectionError.connect(self._on_connection_error)

        # Start the thread and trigger the connection
        self._worker_thread.started.connect(self._worker.do_connect)
        self._worker_thread.start()

    def _on_connection_progress(self, progress, message):
        """Handle connection progress updates from worker"""
        self.connectionProgressChanged.emit(progress)
        self.connectionStatusDetailChanged.emit(message)

    def _on_connection_complete(self, connection, status):
        """Handle connection result on main thread"""
        logger.info(f"[OBD] Connection complete, status: {status}")

        if status == OBDStatus.CAR_CONNECTED:
            self._connection = connection
            self._connected = True
            self._connection_attempts = 0
            self._reconnecting_after_diagnostic = False  # Clear post-diagnostic flag

            # Cache the successful protocol for faster reconnects
            try:
                self._last_successful_protocol = connection.protocol_name()
                logger.info(f"[OBD] Caching successful protocol: {self._last_successful_protocol}")
            except Exception as e:
                logger.debug(f"[OBD] Could not cache protocol name: {e}")

            self.connectionStatusChanged.emit("Connected")
            self.connectionStatusDetailChanged.emit("OBD interface connected successfully")
            self.connectionProgressChanged.emit(100)

            self._setup_watchers()
            self._connection.start()
            self._start_connection_monitor()

            # Auto-scan for supported parameters on connection
            QTimer.singleShot(2000, self.scan_vehicle)

            # Start the data watchdog timer to detect stale connections
            self._last_data_received = time.time()  # Initialize timestamp
            self._data_watchdog_timer.start()
            logger.info("[OBD] Data watchdog started")

            # Stop background scanning while connected
            self._device_scanner_timer.stop()

        elif status == OBDStatus.ELM_CONNECTED:
            self._connection = connection
            self._connected = False

            self.connectionStatusChanged.emit("No Vehicle")
            self.connectionStatusDetailChanged.emit("Connected to adapter, waiting for vehicle...")
            self.connectionProgressChanged.emit(50)

            # Keep trying - vehicle might not be on yet
            # If reconnecting after diagnostic mode, force retry even if auto-reconnect disabled
            if self._reconnecting_after_diagnostic and self._connection_attempts < 3:
                logger.info(f"[OBD] Post-diagnostic retry {self._connection_attempts + 1}/3")
                self.connectionStatusDetailChanged.emit(f"Reconnecting after diagnostic mode... ({self._connection_attempts + 1}/3)")
                QTimer.singleShot(2000, self._start_connection)
            else:
                self._reconnecting_after_diagnostic = False
                self._schedule_auto_reconnect()

        else:
            self._connected = False
            if connection:
                try:
                    connection.close()
                except Exception as e:
                    logger.debug(f"[OBD] Error closing failed connection: {e}")

            self.connectionStatusChanged.emit("Connection Failed")
            self.connectionStatusDetailChanged.emit("Could not connect to OBD adapter")
            self.connectionProgressChanged.emit(0)

            # If reconnecting after diagnostic mode, force retry even if auto-reconnect disabled
            if self._reconnecting_after_diagnostic and self._connection_attempts < 3:
                logger.info(f"[OBD] Post-diagnostic retry {self._connection_attempts + 1}/3")
                self.connectionStatusDetailChanged.emit(f"Reconnecting after diagnostic mode... ({self._connection_attempts + 1}/3)")
                QTimer.singleShot(2000, self._start_connection)
            else:
                self._reconnecting_after_diagnostic = False
                self._schedule_auto_reconnect()

        with self._lock:
            self._is_connecting = False

        # Clean up worker thread
        self._cleanup_worker_thread()

    def _cleanup_worker_thread(self):
        """Clean up the worker thread after connection attempt"""
        if self._worker_thread is not None and self._worker_thread.isRunning():
            self._worker_thread.quit()
            self._worker_thread.wait(2000)
        self._worker_thread = None
        self._worker = None

    def _on_connection_error(self, error_msg):
        """Handle connection error on main thread"""
        logger.error(f"[OBD] Connection error: {error_msg}")
        self._connected = False

        # I/O error (errno 5) means the rfcomm socket is dead - cached protocol is useless
        # Clear it so the next attempt does full protocol detection on a fresh connection
        if "Input/output error" in error_msg or "Errno 5" in error_msg:
            if self._last_successful_protocol:
                logger.info("[OBD] Clearing cached protocol due to I/O error (rfcomm link dropped)")
                self._last_successful_protocol = None

        self.connectionStatusChanged.emit("Error")
        self.connectionStatusDetailChanged.emit(f"Error: {error_msg}")
        self.connectionProgressChanged.emit(0)

        with self._lock:
            self._is_connecting = False

        # Clean up worker thread
        self._cleanup_worker_thread()

        self._schedule_auto_reconnect()

    def _schedule_auto_reconnect(self):
        """Schedule an automatic reconnection attempt"""
        # Check if we've been told to stop completely (e.g., during close())
        if self._force_stop_reconnect:
            logger.info("[OBD] Reconnect stopped by force flag")
            return

        max_attempts = self._get_max_reconnect_attempts()

        # If auto-reconnect is disabled (0), just start device scanner for passive monitoring
        if not self._is_auto_reconnect_enabled():
            logger.info("[OBD] Auto-reconnect disabled, switching to passive scanning")
            self.connectionStatusDetailChanged.emit("Auto-reconnect disabled")
            self._device_scanner_timer.start()
            return

        if self._connection_attempts >= max_attempts:
            logger.info(f"[OBD] Max attempts ({max_attempts}) reached, switching to passive scanning")
            self.connectionStatusDetailChanged.emit("Max retries reached. Scanning...")
            # Don't stop trying completely - just rely on device scanner
            self._device_scanner_timer.start()
            return

        # Calculate backoff delay (2s, 4s, 6s, 8s, 10s max) - reduced for faster reconnects
        delay = min(10, 2 + (self._connection_attempts * 2))
        self.connectionStatusDetailChanged.emit(f"Retry in {delay}s... ({self._connection_attempts}/{max_attempts})")
        logger.info(f"[OBD] Auto-reconnect in {delay}s (attempt {self._connection_attempts + 1}/{max_attempts})")

        QTimer.singleShot(int(delay * 1000), self._start_connection)

    def _start_connection_monitor(self):
        """Start a thread to monitor connection status"""
        if self._monitor_thread and self._monitor_thread.is_alive():
            self._stop_monitor = True
            self._monitor_thread.join(timeout=1.0)

        self._stop_monitor = False
        self._monitor_thread = threading.Thread(target=self._monitor_connection, daemon=True)
        self._monitor_thread.start()

    def _monitor_connection(self):
        """Thread function to monitor connection status"""
        check_interval = 2.0
        conn = self._connection
        if not conn:
            return
        last_status = conn.status()

        while not self._stop_monitor:
            # Grab a local reference each iteration — main thread may set self._connection = None
            conn = self._connection
            if not conn:
                break

            try:
                current_status = conn.status()

                if current_status != last_status:
                    if current_status != OBDStatus.CAR_CONNECTED and last_status == OBDStatus.CAR_CONNECTED:
                        with self._lock:
                            self._connected = False
                        # Use QTimer.singleShot to emit from main thread
                        QTimer.singleShot(0, lambda: self.connectionStatusChanged.emit("Disconnected"))
                        QTimer.singleShot(0, lambda: self.connectionStatusDetailChanged.emit("Connection to vehicle lost"))
                        QTimer.singleShot(0, lambda: self.connectionProgressChanged.emit(0))
                        QTimer.singleShot(0, self._schedule_auto_reconnect)
                        QTimer.singleShot(0, self._device_scanner_timer.start)

                    last_status = current_status

                # Check if device is still available
                port = self._get_configured_port()
                if not self._check_port_exists(port):
                    with self._lock:
                        self._connected = False
                    QTimer.singleShot(0, lambda: self.connectionStatusChanged.emit("Device Lost"))
                    QTimer.singleShot(0, lambda: self.connectionStatusDetailChanged.emit("Bluetooth device disconnected"))
                    QTimer.singleShot(0, lambda: self.connectionProgressChanged.emit(0))
                    QTimer.singleShot(0, lambda: self.devicePresenceChanged.emit(False))
                    QTimer.singleShot(0, self._schedule_auto_reconnect)
                    QTimer.singleShot(0, self._device_scanner_timer.start)
                    break

            except Exception as e:
                logger.info(f"[OBD] Monitor error: {e}")
                break  # Connection likely dead, exit monitor loop

            time.sleep(check_interval)

    def _check_data_watchdog(self):
        """Check if data is still being received - detect stale connections"""
        if not self._connected or not self._connection:
            return

        # If no watchers are active, don't trigger reconnect (no data expected)
        if not self._has_active_watchers:
            return

        current_time = time.time()
        time_since_data = current_time - self._last_data_received

        if self._last_data_received > 0 and time_since_data > self._data_watchdog_timeout:
            logger.info(f"[OBD] Data watchdog triggered - no data for {time_since_data:.1f}s, reconnecting...")
            self.connectionStatusChanged.emit("Stale Connection")
            self.connectionStatusDetailChanged.emit("No data received, reconnecting...")
            self._data_watchdog_timer.stop()
            # Use QTimer.singleShot to avoid threading issues
            QTimer.singleShot(100, self._trigger_watchdog_reconnect)

    def _trigger_watchdog_reconnect(self):
        """Handle reconnect from watchdog timeout"""
        self._cleanup_connection()
        self._connection_attempts = 0  # Reset attempts for fresh start
        self._start_connection()

    def _mark_data_received(self):
        """Called by data callbacks to mark that fresh data was received"""
        self._last_data_received = time.time()

    def _refresh_watchers(self):
        """Refresh OBD watchers when parameters change (no full reconnect needed)"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot refresh watchers - not connected")
            return

        logger.info("[OBD] Refreshing watchers for parameter changes...")

        try:
            # Stop current watching
            self._connection.stop()

            # Unwatch all current commands
            self._connection.unwatch_all()

            # Set up new watchers based on current settings
            self._setup_watchers()

            # Restart watching
            self._connection.start()

            logger.info("[OBD] Watchers refreshed successfully")
        except Exception as e:
            logger.error(f"[OBD] Error refreshing watchers: {e}")

    def _get_all_commands(self):
        """Return dict of all supported OBD commands with their callbacks.
        This is the master list of parameters OCTAVE can monitor."""
        return {
            # Original 18 parameters
            "COOLANT_TEMP": (obd.commands.COOLANT_TEMP, self._update_coolant),
            "CONTROL_MODULE_VOLTAGE": (obd.commands.CONTROL_MODULE_VOLTAGE, self._update_voltage),
            "ENGINE_LOAD": (obd.commands.ENGINE_LOAD, self._update_load),
            "THROTTLE_POS": (obd.commands.THROTTLE_POS, self._update_throttle),
            "INTAKE_TEMP": (obd.commands.INTAKE_TEMP, self._update_intake),
            "TIMING_ADVANCE": (obd.commands.TIMING_ADVANCE, self._update_timing_combined),
            "MAF": (obd.commands.MAF, self._update_maf),
            "SPEED": (obd.commands.SPEED, self._update_speed),
            "RPM": (obd.commands.RPM, self._update_rpm),
            "COMMANDED_EQUIV_RATIO": (obd.commands.COMMANDED_EQUIV_RATIO, self._update_afr),
            "FUEL_LEVEL": (obd.commands.FUEL_LEVEL, self._update_fuel_level),
            "INTAKE_PRESSURE": (obd.commands.INTAKE_PRESSURE, self._update_intake_pressure),
            "SHORT_FUEL_TRIM_1": (obd.commands.SHORT_FUEL_TRIM_1, self._update_short_term_fuel_trim),
            "LONG_FUEL_TRIM_1": (obd.commands.LONG_FUEL_TRIM_1, self._update_long_term_fuel_trim),
            "O2_B1S1": (obd.commands.O2_B1S1, self._update_o2_sensor),
            "FUEL_PRESSURE": (obd.commands.FUEL_PRESSURE, self._update_fuel_pressure),
            "OIL_TEMP": (obd.commands.OIL_TEMP, self._update_oil_temp),
            # Additional parameters
            "RUN_TIME": (obd.commands.RUN_TIME, self._update_run_time),
            "DISTANCE_W_MIL": (obd.commands.DISTANCE_W_MIL, self._update_distance_with_mil),
            "FUEL_RAIL_PRESSURE_VAC": (obd.commands.FUEL_RAIL_PRESSURE_VAC, self._update_fuel_rail_pressure),
            "FUEL_RAIL_PRESSURE_DIRECT": (obd.commands.FUEL_RAIL_PRESSURE_DIRECT, self._update_fuel_rail_pressure_direct),
            "BAROMETRIC_PRESSURE": (obd.commands.BAROMETRIC_PRESSURE, self._update_barometric_pressure),
            "AMBIANT_AIR_TEMP": (obd.commands.AMBIANT_AIR_TEMP, self._update_ambient_air_temp),
            "RELATIVE_THROTTLE_POS": (obd.commands.RELATIVE_THROTTLE_POS, self._update_relative_throttle_pos),
            "THROTTLE_POS_B": (obd.commands.THROTTLE_POS_B, self._update_absolute_throttle_pos_b),
            "ACCELERATOR_POS_D": (obd.commands.ACCELERATOR_POS_D, self._update_accelerator_pos),
            "CATALYST_TEMP_B1S1": (obd.commands.CATALYST_TEMP_B1S1, self._update_catalyst_temp_b1s1),
            "CATALYST_TEMP_B1S2": (obd.commands.CATALYST_TEMP_B1S2, self._update_catalyst_temp_b1s2),
            "EVAP_VAPOR_PRESSURE": (obd.commands.EVAP_VAPOR_PRESSURE, self._update_evap_vapor_pressure),
            "SHORT_FUEL_TRIM_2": (obd.commands.SHORT_FUEL_TRIM_2, self._update_short_fuel_trim_2),
            "LONG_FUEL_TRIM_2": (obd.commands.LONG_FUEL_TRIM_2, self._update_long_fuel_trim_2),
            "O2_B1S2": (obd.commands.O2_B1S2, self._update_o2_sensor_b1s2),
            "O2_B2S1": (obd.commands.O2_B2S1, self._update_o2_sensor_b2s1),
            "O2_B2S2": (obd.commands.O2_B2S2, self._update_o2_sensor_b2s2),
            "DISTANCE_SINCE_DTC_CLEAR": (obd.commands.DISTANCE_SINCE_DTC_CLEAR, self._update_distance_since_codes_cleared),
            "WARMUPS_SINCE_DTC_CLEAR": (obd.commands.WARMUPS_SINCE_DTC_CLEAR, self._update_warmups_since_codes_cleared),
            "ABSOLUTE_LOAD": (obd.commands.ABSOLUTE_LOAD, self._update_absolute_load),
            "COMMANDED_EGR": (obd.commands.COMMANDED_EGR, self._update_commanded_egr),
            "EGR_ERROR": (obd.commands.EGR_ERROR, self._update_egr_error),
            "ETHANOL_PERCENT": (obd.commands.ETHANOL_PERCENT, self._update_ethanol_percent),
            # Batch 2 - Additional O2 sensors
            "O2_B1S3": (obd.commands.O2_B1S3, self._update_o2_sensor_b1s3),
            "O2_B1S4": (obd.commands.O2_B1S4, self._update_o2_sensor_b1s4),
            "O2_B2S3": (obd.commands.O2_B2S3, self._update_o2_sensor_b2s3),
            "O2_B2S4": (obd.commands.O2_B2S4, self._update_o2_sensor_b2s4),
            # Wide-range O2 sensors voltage
            "O2_S1_WR_VOLTAGE": (obd.commands.O2_S1_WR_VOLTAGE, self._update_o2_s1_wr_voltage),
            "O2_S2_WR_VOLTAGE": (obd.commands.O2_S2_WR_VOLTAGE, self._update_o2_s2_wr_voltage),
            "O2_S3_WR_VOLTAGE": (obd.commands.O2_S3_WR_VOLTAGE, self._update_o2_s3_wr_voltage),
            "O2_S4_WR_VOLTAGE": (obd.commands.O2_S4_WR_VOLTAGE, self._update_o2_s4_wr_voltage),
            "O2_S5_WR_VOLTAGE": (obd.commands.O2_S5_WR_VOLTAGE, self._update_o2_s5_wr_voltage),
            "O2_S6_WR_VOLTAGE": (obd.commands.O2_S6_WR_VOLTAGE, self._update_o2_s6_wr_voltage),
            "O2_S7_WR_VOLTAGE": (obd.commands.O2_S7_WR_VOLTAGE, self._update_o2_s7_wr_voltage),
            "O2_S8_WR_VOLTAGE": (obd.commands.O2_S8_WR_VOLTAGE, self._update_o2_s8_wr_voltage),
            # Wide-range O2 sensors current
            "O2_S1_WR_CURRENT": (obd.commands.O2_S1_WR_CURRENT, self._update_o2_s1_wr_current),
            "O2_S2_WR_CURRENT": (obd.commands.O2_S2_WR_CURRENT, self._update_o2_s2_wr_current),
            "O2_S3_WR_CURRENT": (obd.commands.O2_S3_WR_CURRENT, self._update_o2_s3_wr_current),
            "O2_S4_WR_CURRENT": (obd.commands.O2_S4_WR_CURRENT, self._update_o2_s4_wr_current),
            "O2_S5_WR_CURRENT": (obd.commands.O2_S5_WR_CURRENT, self._update_o2_s5_wr_current),
            "O2_S6_WR_CURRENT": (obd.commands.O2_S6_WR_CURRENT, self._update_o2_s6_wr_current),
            "O2_S7_WR_CURRENT": (obd.commands.O2_S7_WR_CURRENT, self._update_o2_s7_wr_current),
            "O2_S8_WR_CURRENT": (obd.commands.O2_S8_WR_CURRENT, self._update_o2_s8_wr_current),
            # Bank 2 catalyst temps
            "CATALYST_TEMP_B2S1": (obd.commands.CATALYST_TEMP_B2S1, self._update_catalyst_temp_b2s1),
            "CATALYST_TEMP_B2S2": (obd.commands.CATALYST_TEMP_B2S2, self._update_catalyst_temp_b2s2),
            # Additional throttle/accelerator
            "THROTTLE_POS_C": (obd.commands.THROTTLE_POS_C, self._update_throttle_pos_c),
            "ACCELERATOR_POS_E": (obd.commands.ACCELERATOR_POS_E, self._update_accelerator_pos_e),
            "ACCELERATOR_POS_F": (obd.commands.ACCELERATOR_POS_F, self._update_accelerator_pos_f),
            "THROTTLE_ACTUATOR": (obd.commands.THROTTLE_ACTUATOR, self._update_throttle_actuator),
            # Fuel system
            "EVAPORATIVE_PURGE": (obd.commands.EVAPORATIVE_PURGE, self._update_evaporative_purge),
            "FUEL_RAIL_PRESSURE_ABS": (obd.commands.FUEL_RAIL_PRESSURE_ABS, self._update_fuel_rail_pressure_abs),
            "FUEL_INJECT_TIMING": (obd.commands.FUEL_INJECT_TIMING, self._update_fuel_inject_timing),
            "FUEL_RATE": (obd.commands.FUEL_RATE, self._update_fuel_rate),
            # Time-based
            "RUN_TIME_MIL": (obd.commands.RUN_TIME_MIL, self._update_run_time_mil),
            "TIME_SINCE_DTC_CLEARED": (obd.commands.TIME_SINCE_DTC_CLEARED, self._update_time_since_dtc_cleared),
            # Other
            "MAX_MAF": (obd.commands.MAX_MAF, self._update_max_maf),
            "FUEL_TYPE": (obd.commands.FUEL_TYPE, self._update_fuel_type),
            "EVAP_VAPOR_PRESSURE_ABS": (obd.commands.EVAP_VAPOR_PRESSURE_ABS, self._update_evap_vapor_pressure_abs),
            "EVAP_VAPOR_PRESSURE_ALT": (obd.commands.EVAP_VAPOR_PRESSURE_ALT, self._update_evap_vapor_pressure_alt),
            "SHORT_O2_TRIM_B1": (obd.commands.SHORT_O2_TRIM_B1, self._update_short_o2_trim_b1),
            "LONG_O2_TRIM_B1": (obd.commands.LONG_O2_TRIM_B1, self._update_long_o2_trim_b1),
            "SHORT_O2_TRIM_B2": (obd.commands.SHORT_O2_TRIM_B2, self._update_short_o2_trim_b2),
            "LONG_O2_TRIM_B2": (obd.commands.LONG_O2_TRIM_B2, self._update_long_o2_trim_b2),
            "RELATIVE_ACCEL_POS": (obd.commands.RELATIVE_ACCEL_POS, self._update_relative_accel_pos),
            "HYBRID_BATTERY_REMAINING": (obd.commands.HYBRID_BATTERY_REMAINING, self._update_hybrid_battery_remaining),
            "ELM_VOLTAGE": (obd.commands.ELM_VOLTAGE, self._update_elm_voltage),
        }

    def _wrap_callback_with_watchdog(self, callback):
        """Wrap a callback to update the data watchdog timestamp"""
        def wrapped(r):
            # Update watchdog timestamp on any response (even null responses)
            self._mark_data_received()
            # Call the original callback
            callback(r)
        return wrapped

    def _setup_watchers(self):
        """Set up watchers based on settings"""
        if not self._connection:
            return

        commands_to_watch = self._get_all_commands()
        watcher_count = 0
        watchdog_attached = False  # Only attach watchdog to ONE param for efficiency

        for param, (command, callback) in commands_to_watch.items():
            should_watch = True
            if self._settings_manager:
                # Default to False for new parameters, True for original ones
                default_enabled = param in [
                    "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "ENGINE_LOAD", "THROTTLE_POS",
                    "INTAKE_TEMP", "TIMING_ADVANCE", "MAF", "SPEED", "RPM", "COMMANDED_EQUIV_RATIO",
                    "FUEL_LEVEL", "INTAKE_PRESSURE", "SHORT_FUEL_TRIM_1", "LONG_FUEL_TRIM_1",
                    "O2_B1S1", "FUEL_PRESSURE", "OIL_TEMP"
                ]
                should_watch = self._settings_manager.get_obd_parameter_enabled(param, default_enabled)

            if should_watch:
                try:
                    # Only wrap ONE callback with watchdog (reduces overhead significantly)
                    # Prefer RPM (typically always enabled), fallback to first available param
                    if not watchdog_attached:
                        if param == "RPM" or watcher_count == 0:
                            final_callback = self._wrap_callback_with_watchdog(callback)
                            watchdog_attached = True
                            logger.info(f"[OBD] Watching: {param} (with watchdog)")
                        else:
                            final_callback = callback
                            logger.debug(f"[OBD] Watching: {param}")
                    else:
                        final_callback = callback
                        logger.debug(f"[OBD] Watching: {param}")
                    # force=True bypasses the supported_commands check in watch().
                    # This prevents silent skip if supported_commands wasn't yet populated
                    # when called from _on_connection_complete (before scan runs).
                    # The async run loop uses force=True internally anyway.
                    self._connection.watch(command, callback=final_callback, force=True)
                    watcher_count += 1
                except Exception as e:
                    logger.warning(f"[OBD] Could not watch {param}: {e}")

        # Track if we have active watchers for the data watchdog
        self._has_active_watchers = watcher_count > 0
        logger.info(f"[OBD] Set up {watcher_count} watchers")

    # Callback functions
    def _update_coolant(self, r):
        if not r.is_null():
            self._coolant_temp = float(r.value.magnitude)
            self.coolantTempChanged.emit(self._coolant_temp)

    def _update_voltage(self, r):
        if not r.is_null():
            self._voltage = float(r.value.magnitude)
            self.voltageChanged.emit(self._voltage)

    def _update_load(self, r):
        if not r.is_null():
            self._engine_load = float(r.value.magnitude)
            self.engineLoadChanged.emit(self._engine_load)

    def _update_throttle(self, r):
        if not r.is_null():
            self._throttle_pos = float(r.value.magnitude)
            self.throttlePositionChanged.emit(self._throttle_pos)

    def _update_intake(self, r):
        if not r.is_null():
            self._intake_temp = float(r.value.magnitude)
            self.intakeAirTempChanged.emit(self._intake_temp)

    def _update_timing_combined(self, r):
        """Combined handler for timing advance and ignition timing (same OBD command)"""
        if not r.is_null():
            value = float(r.value.magnitude)
            self._timing_advance = value
            self._ignition_timing = value
            self.timingAdvanceChanged.emit(self._timing_advance)
            self.ignitionTimingChanged.emit(self._ignition_timing)

    def _update_maf(self, r):
        if not r.is_null():
            self._mass_airflow = float(r.value.magnitude)
            self.massAirFlowChanged.emit(self._mass_airflow)

    def _update_speed(self, r):
        if not r.is_null():
            self._speed_mph = float(r.value.to("mph").magnitude)
            self.speedMPHChanged.emit(self._speed_mph)

    def _update_rpm(self, r):
        if not r.is_null():
            self._rpm = float(r.value.magnitude)
            self.rpmChanged.emit(self._rpm)

    def _update_afr(self, r):
        if not r.is_null():
            self._air_fuel_ratio = float(r.value.magnitude) * 14.7
            self.airFuelRatioChanged.emit(self._air_fuel_ratio)

    def _update_fuel_level(self, r):
        if not r.is_null():
            self._fuel_level = float(r.value.magnitude)
            self.fuelLevelChanged.emit(self._fuel_level)

    def _update_intake_pressure(self, r):
        if not r.is_null():
            self._intake_pressure = float(r.value.magnitude)
            self.intakeManifoldPressureChanged.emit(self._intake_pressure)

    def _update_short_term_fuel_trim(self, r):
        if not r.is_null():
            self._short_term_fuel_trim = float(r.value.magnitude)
            self.shortTermFuelTrimChanged.emit(self._short_term_fuel_trim)

    def _update_long_term_fuel_trim(self, r):
        if not r.is_null():
            self._long_term_fuel_trim = float(r.value.magnitude)
            self.longTermFuelTrimChanged.emit(self._long_term_fuel_trim)

    def _update_o2_sensor(self, r):
        if not r.is_null():
            self._o2_sensor_voltage = float(r.value.magnitude)
            self.oxygenSensorVoltageChanged.emit(self._o2_sensor_voltage)

    def _update_fuel_pressure(self, r):
        if not r.is_null():
            self._fuel_pressure = float(r.value.magnitude)
            self.fuelPressureChanged.emit(self._fuel_pressure)

    def _update_oil_temp(self, r):
        if not r.is_null():
            self._oil_temp = float(r.value.magnitude)
            self.engineOilTempChanged.emit(self._oil_temp)

    # Additional callback functions for new parameters
    def _update_run_time(self, r):
        if not r.is_null():
            self._run_time = float(r.value.magnitude)
            self.runTimeChanged.emit(self._run_time)

    def _update_distance_with_mil(self, r):
        if not r.is_null():
            self._distance_with_mil = float(r.value.magnitude)
            self.distanceWithMILChanged.emit(self._distance_with_mil)

    def _update_fuel_rail_pressure(self, r):
        if not r.is_null():
            self._fuel_rail_pressure = float(r.value.magnitude)
            self.fuelRailPressureChanged.emit(self._fuel_rail_pressure)

    def _update_fuel_rail_pressure_direct(self, r):
        if not r.is_null():
            self._fuel_rail_pressure_direct = float(r.value.magnitude)
            self.fuelRailPressureDirectChanged.emit(self._fuel_rail_pressure_direct)

    def _update_barometric_pressure(self, r):
        if not r.is_null():
            self._barometric_pressure = float(r.value.magnitude)
            self.barometricPressureChanged.emit(self._barometric_pressure)

    def _update_ambient_air_temp(self, r):
        if not r.is_null():
            self._ambient_air_temp = float(r.value.magnitude)
            self.ambientAirTempChanged.emit(self._ambient_air_temp)

    def _update_relative_throttle_pos(self, r):
        if not r.is_null():
            self._relative_throttle_pos = float(r.value.magnitude)
            self.relativeThrottlePosChanged.emit(self._relative_throttle_pos)

    def _update_absolute_throttle_pos_b(self, r):
        if not r.is_null():
            self._absolute_throttle_pos_b = float(r.value.magnitude)
            self.absoluteThrottlePosBChanged.emit(self._absolute_throttle_pos_b)

    def _update_accelerator_pos(self, r):
        if not r.is_null():
            self._accelerator_pos = float(r.value.magnitude)
            self.acceleratorPosChanged.emit(self._accelerator_pos)

    def _update_catalyst_temp_b1s1(self, r):
        if not r.is_null():
            self._catalyst_temp_b1s1 = float(r.value.magnitude)
            self.catalystTempB1S1Changed.emit(self._catalyst_temp_b1s1)

    def _update_catalyst_temp_b1s2(self, r):
        if not r.is_null():
            self._catalyst_temp_b1s2 = float(r.value.magnitude)
            self.catalystTempB1S2Changed.emit(self._catalyst_temp_b1s2)

    def _update_evap_vapor_pressure(self, r):
        if not r.is_null():
            self._evap_vapor_pressure = float(r.value.magnitude)
            self.evapVaporPressureChanged.emit(self._evap_vapor_pressure)

    def _update_short_fuel_trim_2(self, r):
        if not r.is_null():
            self._short_fuel_trim_2 = float(r.value.magnitude)
            self.shortFuelTrim2Changed.emit(self._short_fuel_trim_2)

    def _update_long_fuel_trim_2(self, r):
        if not r.is_null():
            self._long_fuel_trim_2 = float(r.value.magnitude)
            self.longFuelTrim2Changed.emit(self._long_fuel_trim_2)

    def _update_o2_sensor_b1s2(self, r):
        if not r.is_null():
            self._o2_sensor_b1s2 = float(r.value.magnitude)
            self.o2SensorB1S2Changed.emit(self._o2_sensor_b1s2)

    def _update_o2_sensor_b2s1(self, r):
        if not r.is_null():
            self._o2_sensor_b2s1 = float(r.value.magnitude)
            self.o2SensorB2S1Changed.emit(self._o2_sensor_b2s1)

    def _update_o2_sensor_b2s2(self, r):
        if not r.is_null():
            self._o2_sensor_b2s2 = float(r.value.magnitude)
            self.o2SensorB2S2Changed.emit(self._o2_sensor_b2s2)

    def _update_distance_since_codes_cleared(self, r):
        if not r.is_null():
            self._distance_since_codes_cleared = float(r.value.magnitude)
            self.distanceSinceCodesCleared.emit(self._distance_since_codes_cleared)

    def _update_warmups_since_codes_cleared(self, r):
        if not r.is_null():
            self._warmups_since_codes_cleared = float(r.value.magnitude)
            self.warmupsSinceCodesCleared.emit(self._warmups_since_codes_cleared)

    def _update_absolute_load(self, r):
        if not r.is_null():
            self._absolute_load = float(r.value.magnitude)
            self.absoluteLoadChanged.emit(self._absolute_load)

    def _update_commanded_egr(self, r):
        if not r.is_null():
            self._commanded_egr = float(r.value.magnitude)
            self.commandedEGRChanged.emit(self._commanded_egr)

    def _update_egr_error(self, r):
        if not r.is_null():
            self._egr_error = float(r.value.magnitude)
            self.egrErrorChanged.emit(self._egr_error)

    def _update_ethanol_percent(self, r):
        if not r.is_null():
            self._ethanol_percent = float(r.value.magnitude)
            self.ethanoPercentChanged.emit(self._ethanol_percent)

    # Batch 2 callback functions - Additional O2 sensors
    def _update_o2_sensor_b1s3(self, r):
        if not r.is_null():
            self._o2_sensor_b1s3 = float(r.value.magnitude)
            self.o2SensorB1S3Changed.emit(self._o2_sensor_b1s3)

    def _update_o2_sensor_b1s4(self, r):
        if not r.is_null():
            self._o2_sensor_b1s4 = float(r.value.magnitude)
            self.o2SensorB1S4Changed.emit(self._o2_sensor_b1s4)

    def _update_o2_sensor_b2s3(self, r):
        if not r.is_null():
            self._o2_sensor_b2s3 = float(r.value.magnitude)
            self.o2SensorB2S3Changed.emit(self._o2_sensor_b2s3)

    def _update_o2_sensor_b2s4(self, r):
        if not r.is_null():
            self._o2_sensor_b2s4 = float(r.value.magnitude)
            self.o2SensorB2S4Changed.emit(self._o2_sensor_b2s4)

    # Wide-range O2 sensors voltage
    def _update_o2_s1_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s1_wr_voltage = float(r.value.magnitude)
            self.o2S1WRVoltageChanged.emit(self._o2_s1_wr_voltage)

    def _update_o2_s2_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s2_wr_voltage = float(r.value.magnitude)
            self.o2S2WRVoltageChanged.emit(self._o2_s2_wr_voltage)

    def _update_o2_s3_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s3_wr_voltage = float(r.value.magnitude)
            self.o2S3WRVoltageChanged.emit(self._o2_s3_wr_voltage)

    def _update_o2_s4_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s4_wr_voltage = float(r.value.magnitude)
            self.o2S4WRVoltageChanged.emit(self._o2_s4_wr_voltage)

    def _update_o2_s5_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s5_wr_voltage = float(r.value.magnitude)
            self.o2S5WRVoltageChanged.emit(self._o2_s5_wr_voltage)

    def _update_o2_s6_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s6_wr_voltage = float(r.value.magnitude)
            self.o2S6WRVoltageChanged.emit(self._o2_s6_wr_voltage)

    def _update_o2_s7_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s7_wr_voltage = float(r.value.magnitude)
            self.o2S7WRVoltageChanged.emit(self._o2_s7_wr_voltage)

    def _update_o2_s8_wr_voltage(self, r):
        if not r.is_null():
            self._o2_s8_wr_voltage = float(r.value.magnitude)
            self.o2S8WRVoltageChanged.emit(self._o2_s8_wr_voltage)

    # Wide-range O2 sensors current
    def _update_o2_s1_wr_current(self, r):
        if not r.is_null():
            self._o2_s1_wr_current = float(r.value.magnitude)
            self.o2S1WRCurrentChanged.emit(self._o2_s1_wr_current)

    def _update_o2_s2_wr_current(self, r):
        if not r.is_null():
            self._o2_s2_wr_current = float(r.value.magnitude)
            self.o2S2WRCurrentChanged.emit(self._o2_s2_wr_current)

    def _update_o2_s3_wr_current(self, r):
        if not r.is_null():
            self._o2_s3_wr_current = float(r.value.magnitude)
            self.o2S3WRCurrentChanged.emit(self._o2_s3_wr_current)

    def _update_o2_s4_wr_current(self, r):
        if not r.is_null():
            self._o2_s4_wr_current = float(r.value.magnitude)
            self.o2S4WRCurrentChanged.emit(self._o2_s4_wr_current)

    def _update_o2_s5_wr_current(self, r):
        if not r.is_null():
            self._o2_s5_wr_current = float(r.value.magnitude)
            self.o2S5WRCurrentChanged.emit(self._o2_s5_wr_current)

    def _update_o2_s6_wr_current(self, r):
        if not r.is_null():
            self._o2_s6_wr_current = float(r.value.magnitude)
            self.o2S6WRCurrentChanged.emit(self._o2_s6_wr_current)

    def _update_o2_s7_wr_current(self, r):
        if not r.is_null():
            self._o2_s7_wr_current = float(r.value.magnitude)
            self.o2S7WRCurrentChanged.emit(self._o2_s7_wr_current)

    def _update_o2_s8_wr_current(self, r):
        if not r.is_null():
            self._o2_s8_wr_current = float(r.value.magnitude)
            self.o2S8WRCurrentChanged.emit(self._o2_s8_wr_current)

    # Bank 2 catalyst temps
    def _update_catalyst_temp_b2s1(self, r):
        if not r.is_null():
            self._catalyst_temp_b2s1 = float(r.value.magnitude)
            self.catalystTempB2S1Changed.emit(self._catalyst_temp_b2s1)

    def _update_catalyst_temp_b2s2(self, r):
        if not r.is_null():
            self._catalyst_temp_b2s2 = float(r.value.magnitude)
            self.catalystTempB2S2Changed.emit(self._catalyst_temp_b2s2)

    # Additional throttle/accelerator
    def _update_throttle_pos_c(self, r):
        if not r.is_null():
            self._throttle_pos_c = float(r.value.magnitude)
            self.throttlePosCChanged.emit(self._throttle_pos_c)

    def _update_accelerator_pos_e(self, r):
        if not r.is_null():
            self._accelerator_pos_e = float(r.value.magnitude)
            self.acceleratorPosEChanged.emit(self._accelerator_pos_e)

    def _update_accelerator_pos_f(self, r):
        if not r.is_null():
            self._accelerator_pos_f = float(r.value.magnitude)
            self.acceleratorPosFChanged.emit(self._accelerator_pos_f)

    def _update_throttle_actuator(self, r):
        if not r.is_null():
            self._throttle_actuator = float(r.value.magnitude)
            self.throttleActuatorChanged.emit(self._throttle_actuator)

    # Fuel system
    def _update_evaporative_purge(self, r):
        if not r.is_null():
            self._evaporative_purge = float(r.value.magnitude)
            self.evaporativePurgeChanged.emit(self._evaporative_purge)

    def _update_fuel_rail_pressure_abs(self, r):
        if not r.is_null():
            self._fuel_rail_pressure_abs = float(r.value.magnitude)
            self.fuelRailPressureAbsChanged.emit(self._fuel_rail_pressure_abs)

    def _update_fuel_inject_timing(self, r):
        if not r.is_null():
            self._fuel_inject_timing = float(r.value.magnitude)
            self.fuelInjectTimingChanged.emit(self._fuel_inject_timing)

    def _update_fuel_rate(self, r):
        if not r.is_null():
            self._fuel_rate = float(r.value.magnitude)
            self.fuelRateChanged.emit(self._fuel_rate)

    # Time-based
    def _update_run_time_mil(self, r):
        if not r.is_null():
            self._run_time_mil = float(r.value.magnitude)
            self.runTimeMILChanged.emit(self._run_time_mil)

    def _update_time_since_dtc_cleared(self, r):
        if not r.is_null():
            self._time_since_dtc_cleared = float(r.value.magnitude)
            self.timeSinceDTCClearedChanged.emit(self._time_since_dtc_cleared)

    # Other
    def _update_max_maf(self, r):
        if not r.is_null():
            self._max_maf = float(r.value.magnitude)
            self.maxMAFChanged.emit(self._max_maf)

    def _update_fuel_type(self, r):
        if not r.is_null():
            self._fuel_type = float(r.value.magnitude)
            self.fuelTypeChanged.emit(self._fuel_type)

    def _update_evap_vapor_pressure_abs(self, r):
        if not r.is_null():
            self._evap_vapor_pressure_abs = float(r.value.magnitude)
            self.evapVaporPressureAbsChanged.emit(self._evap_vapor_pressure_abs)

    def _update_evap_vapor_pressure_alt(self, r):
        if not r.is_null():
            self._evap_vapor_pressure_alt = float(r.value.magnitude)
            self.evapVaporPressureAltChanged.emit(self._evap_vapor_pressure_alt)

    def _update_short_o2_trim_b1(self, r):
        if not r.is_null():
            self._short_o2_trim_b1 = float(r.value.magnitude)
            self.shortO2TrimB1Changed.emit(self._short_o2_trim_b1)

    def _update_long_o2_trim_b1(self, r):
        if not r.is_null():
            self._long_o2_trim_b1 = float(r.value.magnitude)
            self.longO2TrimB1Changed.emit(self._long_o2_trim_b1)

    def _update_short_o2_trim_b2(self, r):
        if not r.is_null():
            self._short_o2_trim_b2 = float(r.value.magnitude)
            self.shortO2TrimB2Changed.emit(self._short_o2_trim_b2)

    def _update_long_o2_trim_b2(self, r):
        if not r.is_null():
            self._long_o2_trim_b2 = float(r.value.magnitude)
            self.longO2TrimB2Changed.emit(self._long_o2_trim_b2)

    def _update_relative_accel_pos(self, r):
        if not r.is_null():
            self._relative_accel_pos = float(r.value.magnitude)
            self.relativeAccelPosChanged.emit(self._relative_accel_pos)

    def _update_hybrid_battery_remaining(self, r):
        if not r.is_null():
            self._hybrid_battery_remaining = float(r.value.magnitude)
            self.hybridBatteryRemainingChanged.emit(self._hybrid_battery_remaining)

    def _update_elm_voltage(self, r):
        if not r.is_null():
            self._elm_voltage = float(r.value.magnitude)
            self.elmVoltageChanged.emit(self._elm_voltage)

    # Getter methods
    @Slot(result=float)
    def coolantTemp(self):
        return self._coolant_temp

    @Slot(result=float)
    def voltage(self):
        return self._voltage

    @Slot(result=float)
    def engineLoad(self):
        return self._engine_load

    @Slot(result=float)
    def throttlePosition(self):
        return self._throttle_pos

    @Slot(result=float)
    def intakeTemp(self):
        return self._intake_temp

    @Slot(result=float)
    def timingAdvance(self):
        return self._timing_advance

    @Slot(result=float)
    def massAirFlow(self):
        return self._mass_airflow

    @Slot(result=float)
    def speedMPH(self):
        return self._speed_mph

    @Slot(result=float)
    def rpm(self):
        return self._rpm

    @Slot(result=float)
    def airFuelRatio(self):
        return self._air_fuel_ratio

    @Slot(result=float)
    def fuelLevel(self):
        return self._fuel_level

    @Slot(result=float)
    def intakeManifoldPressure(self):
        return self._intake_pressure

    @Slot(result=float)
    def shortTermFuelTrim(self):
        return self._short_term_fuel_trim

    @Slot(result=float)
    def longTermFuelTrim(self):
        return self._long_term_fuel_trim

    @Slot(result=float)
    def oxygenSensorVoltage(self):
        return self._o2_sensor_voltage

    @Slot(result=float)
    def fuelPressure(self):
        return self._fuel_pressure

    @Slot(result=float)
    def engineOilTemp(self):
        return self._oil_temp

    @Slot(result=float)
    def ignitionTiming(self):
        return self._ignition_timing

    @Slot()
    def reconnect(self):
        """Attempt to reconnect to the OBD device"""
        logger.info("[OBD] Manual reconnect requested")
        self._cleanup_connection()
        self._connection_attempts = 0
        self._start_connection()

    @Slot()
    def force_connect(self):
        """Force a connection attempt, bypassing backoff and resetting attempt counter"""
        logger.info("[OBD] Force connect requested")
        self._connection_attempts = 0
        self._cleanup_connection()
        self._start_connection()

    @Slot()
    def refresh_values(self):
        """Re-emit the current value of every watched PID (C++ parity).

        The home screen calls this after homeOBDParametersChanged so tiles that
        were just enabled get a value immediately instead of waiting for the
        next change-driven callback. python-OBD's Async.query() is non-blocking
        and returns the cached response, so this never touches the adapter.
        """
        conn = self._connection
        if not conn or not getattr(conn, "running", False):
            return
        try:
            commands = self._get_all_commands()
        except Exception as e:
            logger.debug(f"[OBD] refresh_values: could not build command map: {e}")
            return
        for param, (command, callback) in commands.items():
            try:
                response = conn.query(command)
                if response is not None and not response.is_null():
                    callback(response)
            except Exception as e:
                logger.debug(f"[OBD] refresh_values: {param}: {e}")

    @Slot(str)
    def set_target_address(self, address):
        """Desktop: persist the adapter identifier as the OBD port (C++ parity).
        The port-change watcher in the settings manager then triggers a reconnect."""
        logger.info(f"[OBD] set_target_address: {address}")
        if self._settings_manager:
            self._settings_manager.save_obd_bluetooth_port(address)

    def _cache_connection_detail(self, detail):
        self._connection_detail = detail

    def _cache_connection_progress(self, progress):
        self._connection_progress = progress

    @Property(str, notify=connectionStatusDetailChanged)
    def connectionDetail(self):
        return self._connection_detail

    @Property(int, notify=connectionProgressChanged)
    def connectionProgress(self):
        return self._connection_progress

    def _cleanup_connection(self):
        """Clean up existing connection before reconnecting"""
        # Stop the data watchdog timer
        self._data_watchdog_timer.stop()
        self._last_data_received = 0

        self._stop_monitor = True
        if self._monitor_thread and self._monitor_thread.is_alive():
            self._monitor_thread.join(timeout=1.0)
        if self._connection:
            try:
                self._connection.stop()
                self._connection.close()
            except Exception as e:
                logger.debug(f"[OBD] Error closing connection during cleanup: {e}")
            self._connection = None
        self._connected = False

    @Slot(result=bool)
    def is_connected(self):
        """Return current connection status"""
        return self._connected

    @Slot(result=str)
    def get_connection_status(self):
        """Get detailed connection status"""
        if not self._connection:
            return "No Connection"
        return str(self._connection.status())

    @Slot()
    def close(self):
        """Cleanup connection"""
        self._force_stop_reconnect = True
        self._device_scanner_timer.stop()
        self._cleanup_connection()

    @Slot()
    def reset_connection(self):
        """Hard reset the connection, resetting the attempt counter"""
        self.force_connect()

    @Slot(result=bool)
    def check_device_presence(self):
        """Check if the configured device is present"""
        port = self._get_configured_port()
        device_present = self._check_port_exists(port)
        self.devicePresenceChanged.emit(device_present)
        return device_present

    @Slot(result=list)
    def get_available_ports(self):
        """Get list of available serial/OBD ports"""
        self._scan_for_devices()
        return self._available_ports

    # ──────────────────────────────────────────────────────────────────
    # Unified adapter API — mirrors the C++ OBDManager so the QML OBD
    # settings page calls the same names against either backend. All
    # platform variation (rfcomm bind on Linux, COM lookup on Windows,
    # paired-list on macOS) lives in this manager — never in QML.
    # ──────────────────────────────────────────────────────────────────

    _MAC_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")

    def _kind_for_identifier(self, identifier):
        if self._MAC_RE.match(identifier):
            return "ble"
        if identifier.startswith("/dev/rfcomm"):
            return "serial-rfcomm"
        if identifier.startswith("/dev/ttyUSB") or identifier.startswith("/dev/ttyACM"):
            return "serial-usb"
        if identifier.startswith("/dev/tty.") or identifier.startswith("/dev/cu."):
            return "tty"
        if identifier.startswith("COM"):
            return "com"
        return "serial"

    def _display_name_for_identifier(self, identifier):
        kind = self._kind_for_identifier(identifier)
        if kind == "ble":            return f"Bluetooth · {identifier}"
        if kind == "serial-rfcomm":  return f"Bluetooth (bound) · {identifier}"
        if kind == "serial-usb":     return f"USB ELM327 · {identifier}"
        if kind == "tty":            return identifier
        if kind == "com":            return f"Serial · {identifier}"
        return identifier

    def _build_adapter_list(self, ports):
        return [
            {
                "identifier": p,
                "name": self._display_name_for_identifier(p),
                "kind": self._kind_for_identifier(p),
            }
            for p in ports
        ]

    @Property(list, notify=availableAdaptersChanged)
    def availableAdapters(self):
        return self._build_adapter_list(self._available_ports)

    @Slot()
    def scan(self):
        """Trigger a discovery sweep — same name + behaviour as C++."""
        self._scan_for_devices()

    @Slot(result=str)
    def platform_hint(self):
        return self._get_platform()  # 'windows' | 'macos' | 'linux'

    def _ensure_rfcomm_bound(self, mac):
        """Linux/Pi: translate a MAC into a /dev/rfcommN node, binding it via
        `rfcomm bind 0 <mac> 1` if no node already exists. Returns the path on
        success, None on failure (with reason logged to the connection-log)."""
        # Existing bound node wins — user may have pre-bound manually.
        for i in range(8):
            dev = f"/dev/rfcomm{i}"
            if os.path.exists(dev):
                self.connectionLogLineAppended.emit(f"rfcomm: using existing {dev}")
                return dev

        if shutil.which("rfcomm") is None:
            self.connectionLogLineAppended.emit(
                "rfcomm bind failed: rfcomm binary not found "
                "(install bluez-utils, or pre-bind and enter /dev/rfcomm0)")
            return None

        self.connectionLogLineAppended.emit(f"rfcomm: binding {mac} → /dev/rfcomm0")
        try:
            result = subprocess.run(
                ["rfcomm", "bind", "0", mac, "1"],
                capture_output=True, text=True, timeout=5)
        except subprocess.TimeoutExpired:
            self.connectionLogLineAppended.emit("rfcomm bind failed: timed out")
            return None
        except OSError as e:
            self.connectionLogLineAppended.emit(f"rfcomm bind failed: {e}")
            return None
        if result.returncode != 0:
            err = (result.stderr or "").strip() or "non-zero exit"
            self.connectionLogLineAppended.emit(f"rfcomm bind failed: {err}")
            self.connectionLogLineAppended.emit(
                f"tip: add user to bluetooth group, or run "
                f"`sudo rfcomm bind 0 {mac}` once and enter /dev/rfcomm0")
            return None
        # Wait for the node to appear (rfcomm sometimes succeeds async).
        for _ in range(10):
            if os.path.exists("/dev/rfcomm0"):
                self.connectionLogLineAppended.emit("rfcomm: bound, opening /dev/rfcomm0")
                return "/dev/rfcomm0"
            time.sleep(0.1)
        self.connectionLogLineAppended.emit(
            "rfcomm bind reported success but /dev/rfcomm0 missing")
        return None

    @Slot(str)
    def connect_to_adapter(self, identifier):
        """One-shot connect: normalise the identifier, save it, optionally
        rfcomm-bind on Linux, then force_connect(). The single entry point
        the QML uses for both adapter-row taps and the manual-entry Go button."""
        trimmed = (identifier or "").strip()
        if not trimmed:
            logger.debug("[OBD] connect_to_adapter: empty identifier, ignoring")
            return

        is_mac = bool(self._MAC_RE.match(trimmed))
        kind = self._kind_for_identifier(trimmed)
        logger.info(f"[OBD] connect_to_adapter: {trimmed} (kind: {kind})")
        self.connectionLogLineAppended.emit(
            f"connect → {self._display_name_for_identifier(trimmed)}")

        if self._settings_manager:
            self._settings_manager.save_obd_bluetooth_port(trimmed)
            if is_mac:
                self._settings_manager.add_obd_saved_adapter(trimmed, trimmed)

        if self._get_platform() == "linux" and is_mac:
            bound = self._ensure_rfcomm_bound(trimmed)
            if bound and self._settings_manager:
                self._settings_manager.save_obd_bluetooth_port(bound)
            elif bound is None:
                self.connectionStatusChanged.emit("Error")
                self.connectionStatusDetailChanged.emit(
                    "rfcomm bind failed — see Connection Log")
                return

        self.force_connect()

    @Slot(bool)
    def set_auto_reconnect(self, enabled):
        """Deprecated: Auto-reconnect is now controlled via settings (obdAutoReconnectAttempts)"""
        logger.info("[OBD] Warning: set_auto_reconnect() is deprecated. Use Settings > OBD > Auto-Reconnect Attempts instead.")

    @Slot(int)
    def set_connection_timeout(self, timeout_seconds):
        """Set the connection timeout"""
        self._connection_timeout = max(5, min(60, timeout_seconds))

    # ==================== Vehicle Scan Functions ====================

    @Slot(result=list)
    def get_all_parameter_names(self):
        """Get list of all parameter names that OCTAVE supports"""
        return list(self._get_all_commands().keys())

    @Slot(result=list)
    def get_supported_commands(self):
        """Get list of commands supported by the connected vehicle"""
        return self._supported_commands

    @Slot(result=bool)
    def is_scanning(self):
        """Check if a vehicle scan is currently in progress"""
        return self._is_scanning

    @Slot()
    def scan_vehicle(self):
        """Scan the connected vehicle for supported OBD commands.
        This queries the vehicle to find which PIDs it supports."""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot scan - not connected to vehicle")
            self.scanProgressChanged.emit(0, "Not connected to vehicle")
            return

        if self._is_scanning:
            logger.info("[OBD] Scan already in progress")
            return

        self._is_scanning = True
        self.scanProgressChanged.emit(0, "Starting vehicle scan...")

        # Run scan in a separate thread to avoid blocking UI
        scan_thread = threading.Thread(target=self._do_vehicle_scan, daemon=True)
        scan_thread.start()

    def _emit_scan_output(self, message: str):
        """Emit scan output on main thread"""
        QTimer.singleShot(0, lambda m=message: self.scanOutputChanged.emit(m))

    def _do_vehicle_scan(self):
        """Perform the actual vehicle scan (runs in background thread).

        Reads supported_commands from the existing async connection's cached set
        (populated during connection init). This avoids stopping async polling or
        opening a second sync connection on the same serial port, keeping the
        live data stream uninterrupted.
        """
        try:
            self._emit_scan_output("[SCAN] Starting vehicle PID scan...")
            self._emit_scan_output("[INFO] Reading supported commands from active connection...")

            # Read the cached supported_commands set from the existing connection.
            # obd.Async inherits from obd.OBD; supported_commands is populated
            # during __init__ — it's a frozen set, safe to read from any thread.
            conn = self._connection
            if not conn:
                self._emit_scan_output("[WARN] Connection lost before scan could start")
                QTimer.singleShot(0, lambda: self.scanProgressChanged.emit(0, "Connection lost"))
                return
            supported = conn.supported_commands

            if not supported:
                self._emit_scan_output("[WARN] No supported commands returned from vehicle")
                QTimer.singleShot(0, lambda: self.scanProgressChanged.emit(100, "No supported commands found"))
                QTimer.singleShot(0, lambda: self.scanCompleteChanged.emit([]))
                return

            self._emit_scan_output(f"[INFO] Vehicle reports {len(supported)} total supported PIDs")
            self._emit_scan_output("[SCAN] Checking available parameters...")
            self._emit_scan_output("")

            # Get the commands we can actually use (intersection with our supported commands)
            our_commands = self._get_all_commands()
            supported_names = []
            unsupported_names = []

            total = len(our_commands)
            for i, (param_name, (command, _)) in enumerate(our_commands.items()):
                progress = int((i / total) * 100)
                QTimer.singleShot(0, lambda p=progress, n=param_name: self.scanProgressChanged.emit(p, f"Checking {n}..."))

                # Check if this command is in the vehicle's supported set
                if command in supported:
                    supported_names.append(param_name)
                    self._emit_scan_output(f"[OK] {param_name}")
                    logger.info(f"[OBD] Vehicle supports: {param_name}")
                else:
                    unsupported_names.append(param_name)

            self._supported_commands = supported_names

            # Persist scan results so they survive app restarts
            if self._settings_manager:
                QTimer.singleShot(0, lambda names=list(supported_names):
                    self._settings_manager.save_supported_obd_parameters(names))

            # Summary output
            self._emit_scan_output("")
            self._emit_scan_output("=" * 40)
            self._emit_scan_output(f"[DONE] Scan complete!")
            self._emit_scan_output(f"[INFO] Supported: {len(supported_names)} parameters")
            self._emit_scan_output(f"[INFO] Unsupported: {len(unsupported_names)} parameters")
            self._emit_scan_output("=" * 40)

            # Emit completion signals on main thread
            QTimer.singleShot(0, lambda: self.scanProgressChanged.emit(100, f"Found {len(supported_names)} supported parameters"))
            QTimer.singleShot(0, lambda: self.supportedCommandsChanged.emit(supported_names))
            QTimer.singleShot(0, lambda: self.scanCompleteChanged.emit(supported_names))

            # Refresh watchers now that we know what's supported
            # This ensures watchers are active even if _setup_watchers() was called before
            # supported_commands was fully populated, or if async was running during setup
            QTimer.singleShot(500, self._refresh_watchers)

            logger.info(f"[OBD] Vehicle scan complete: {len(supported_names)} supported parameters")

        except Exception as e:
            logger.info(f"[OBD] Scan error: {e}")
            self._emit_scan_output(f"[ERROR] Scan failed: {e}")
            # Bind `e` into a default arg — except-scope variables are cleared
            # when the block exits, so the lambda would otherwise reference a
            # deleted name when QTimer fires it.
            err_msg = str(e)
            QTimer.singleShot(0, lambda msg=err_msg: self.scanProgressChanged.emit(0, f"Scan error: {msg}"))

        finally:
            self._is_scanning = False

    @Slot(list)
    def enable_scanned_parameters(self, param_names):
        """Enable all the parameters found during scan"""
        if not self._settings_manager:
            return

        for param in param_names:
            self._settings_manager.save_obd_parameter_enabled(param, True)

        logger.info(f"[OBD] Enabled {len(param_names)} scanned parameters")

    @Slot()
    def enable_all_supported(self):
        """Enable all parameters that the vehicle supports"""
        if self._supported_commands:
            self.enable_scanned_parameters(self._supported_commands)

    # ==================== Diagnostic Mode Management ====================

    @Slot()
    def enter_diagnostic_mode(self):
        """Enter diagnostic mode - pauses async polling and creates a persistent sync connection.
        Call this when entering the OBD Diagnostics menu."""
        with self._diagnostic_mode_lock:
            if self._diagnostic_mode:
                logger.info("[OBD] Already in diagnostic mode")
                return

            if self._diagnostic_mode_transitioning:
                logger.info("[OBD] Diagnostic mode transition already in progress, ignoring")
                return

            if not self._connection or not self._connected:
                logger.info("[OBD] Cannot enter diagnostic mode - not connected")
                return

            self._diagnostic_mode_transitioning = True

        logger.info("[OBD] Entering diagnostic mode - pausing async polling...")

        try:
            # Stop the data watchdog timer to prevent false reconnects
            self._data_watchdog_timer.stop()
            logger.info("[OBD] Data watchdog paused for diagnostic mode")

            # Stop async polling
            self._connection.stop()

            # Wait for port to be released (reduced from 0.5s)
            time.sleep(0.2)

            # Check if we were cancelled during the wait (rapid menu switching)
            with self._diagnostic_mode_lock:
                if not self._diagnostic_mode_transitioning:
                    logger.info("[OBD] Diagnostic mode entry cancelled")
                    self._connection.start()  # Resume async since we stopped it
                    # Resume watchdog since we're not entering diagnostic mode
                    self._last_data_received = time.time()
                    self._data_watchdog_timer.start()
                    return

            # Create a persistent sync connection for diagnostic queries
            port = self._get_configured_port()
            logger.info(f"[OBD] Creating diagnostic sync connection on port: {port}")
            try:
                self._diagnostic_sync_conn = obd.OBD(port, fast=False, timeout=10)
                if self._diagnostic_sync_conn.is_connected():
                    logger.info(f"[OBD] Diagnostic mode active - sync connection ready on {port}")
                    logger.info(f"[OBD] Diagnostic sync connection status: {self._diagnostic_sync_conn.status()}")
                    logger.info(f"[OBD] Diagnostic sync connection protocol: {self._diagnostic_sync_conn.protocol_name()}")
                else:
                    logger.info(f"[OBD] Warning: Diagnostic sync connection failed on {port}")
                    logger.info(f"[OBD] Connection status: {self._diagnostic_sync_conn.status()}")
                    self._diagnostic_sync_conn.close()
                    self._diagnostic_sync_conn = None
            except Exception as e:
                logger.error(f"[OBD] Error creating diagnostic sync connection: {e}")
                self._diagnostic_sync_conn = None

            with self._diagnostic_mode_lock:
                self._diagnostic_mode = True
                self._diagnostic_mode_transitioning = False

        except Exception as e:
            logger.error(f"[OBD] Error entering diagnostic mode: {e}")
            with self._diagnostic_mode_lock:
                self._diagnostic_mode_transitioning = False
            # Try to resume async polling and watchdog on error
            if self._connection:
                try:
                    self._connection.start()
                    self._last_data_received = time.time()
                    self._data_watchdog_timer.start()
                except Exception as resume_err:
                    logger.error(
                        f"[OBD] Failed to resume async polling after diagnostic entry error: {resume_err}"
                    )

    @Slot()
    def exit_diagnostic_mode(self):
        """Exit diagnostic mode - closes sync connection and resumes async polling.
        Call this when leaving the OBD Diagnostics menu."""
        with self._diagnostic_mode_lock:
            if not self._diagnostic_mode and not self._diagnostic_mode_transitioning:
                logger.info("[OBD] Not in diagnostic mode")
                return

            # Cancel any in-progress entry. This is the cancellation handshake for
            # rapid menu switches: enter_diagnostic_mode sets _transitioning=True,
            # releases the lock to do blocking I/O (connection.stop + 0.2s sleep),
            # then re-checks the flag at L1770. If we clear it here during that
            # window, enter_diagnostic_mode bails out and restores async polling.
            if self._diagnostic_mode_transitioning and not self._diagnostic_mode:
                logger.info("[OBD] Cancelling diagnostic mode entry in progress")
                self._diagnostic_mode_transitioning = False
                return

            self._diagnostic_mode_transitioning = True

        logger.info("[OBD] Exiting diagnostic mode - resuming async polling...")

        try:
            # Close the diagnostic sync connection
            if self._diagnostic_sync_conn:
                try:
                    self._diagnostic_sync_conn.close()
                    logger.info("[OBD] Diagnostic sync connection closed")
                except Exception as e:
                    logger.error(f"[OBD] Error closing diagnostic sync connection: {e}")
                self._diagnostic_sync_conn = None

            with self._diagnostic_mode_lock:
                self._diagnostic_mode = False
                self._diagnostic_mode_transitioning = False

            # The diagnostic sync connection likely corrupted the async connection's
            # serial port state. We need to do a full reconnect to restore data flow.
            # Simply calling start() on the old connection won't work reliably.
            was_connected = self._connected
            if self._connection or was_connected:
                logger.info("[OBD] Performing full reconnect after diagnostic mode...")
                # Close the old async connection completely
                if self._connection:
                    try:
                        self._connection.close()
                        logger.info("[OBD] Old async connection closed")
                    except Exception as e:
                        logger.error(f"[OBD] Error closing old async connection: {e}")
                    self._connection = None

                # Mark as disconnected during reconnect
                self._connected = False
                self.connectionStatusChanged.emit("Reconnecting")
                self.connectionStatusDetailChanged.emit("Reconnecting after diagnostic mode...")

                # Reset _is_connecting flag to ensure reconnection isn't blocked
                # This is needed in case a previous connection attempt left it stuck
                with self._lock:
                    if self._is_connecting:
                        logger.info("[OBD] Resetting stuck _is_connecting flag")
                    self._is_connecting = False

                # Use QTimer.singleShot to avoid blocking the Qt event loop
                # This gives the serial port time to be released without freezing the UI
                # Delay of 1000ms allows adapter to fully reset after diagnostic mode
                self._connection_attempts = 0
                QTimer.singleShot(1000, self._delayed_reconnect_after_diagnostic)
                logger.info("[OBD] Scheduled reconnection in 1000ms")
            else:
                logger.info("[OBD] No connection to resume")

        except Exception as e:
            logger.error(f"[OBD] Error exiting diagnostic mode: {e}")
            with self._diagnostic_mode_lock:
                self._diagnostic_mode = False
                self._diagnostic_mode_transitioning = False
            # Try to reconnect on error using delayed approach
            with self._lock:
                self._is_connecting = False
            self._connection_attempts = 0
            QTimer.singleShot(1000, self._delayed_reconnect_after_diagnostic)

    def _delayed_reconnect_after_diagnostic(self):
        """Called after a delay to reconnect following diagnostic mode exit.
        This runs on the main thread via QTimer, avoiding event loop blocking."""
        logger.info("[OBD] Executing delayed reconnect after diagnostic mode...")
        try:
            # Double-check we're not in diagnostic mode (user might have re-entered)
            if self._diagnostic_mode:
                logger.info("[OBD] Back in diagnostic mode, skipping reconnect")
                self._reconnecting_after_diagnostic = False
                return

            # Ensure connection flags are in correct state
            with self._lock:
                self._is_connecting = False

            # Clear cached protocol to force fresh negotiation after diagnostic mode
            # The adapter may be in an inconsistent state after sync connection
            self._last_successful_protocol = None
            logger.info("[OBD] Cleared cached protocol for fresh negotiation")

            # Mark that we're reconnecting after diagnostic mode - allows retries
            # even if auto-reconnect is disabled in settings
            self._reconnecting_after_diagnostic = True

            self._start_connection()
            logger.info("[OBD] Fresh async connection initiated")
        except Exception as e:
            logger.error(f"[OBD] Error in delayed reconnect: {e}")
            # Schedule another attempt
            QTimer.singleShot(1000, self._delayed_reconnect_after_diagnostic)

    @Slot(result=bool)
    def is_diagnostic_mode(self):
        """Check if currently in diagnostic mode"""
        return self._diagnostic_mode

    # ==================== Diagnostic Commands ====================

    def _get_diagnostic_connection(self):
        """Get the appropriate connection for diagnostic queries.
        Uses persistent sync connection if in diagnostic mode, otherwise creates a temporary one."""
        if self._diagnostic_mode and self._diagnostic_sync_conn:
            return self._diagnostic_sync_conn
        return None

    def _create_sync_connection(self):
        """Create a temporary synchronous OBD connection for diagnostic queries.
        This mirrors the approach used in test scripts that work reliably."""
        port = self._get_configured_port()
        try:
            # Wait for async connection to fully release the port (reduced from 0.5s)
            time.sleep(0.2)

            # Create a simple synchronous connection like the test scripts
            sync_conn = obd.OBD(port, fast=False, timeout=10)
            if sync_conn.is_connected():
                return sync_conn
            else:
                logger.info(f"[OBD] Sync connection failed to connect on {port}")
                sync_conn.close()
                return None
        except Exception as e:
            logger.error(f"[OBD] Error creating sync connection: {e}")
            return None

    @Slot()
    def read_dtc(self):
        """Read Diagnostic Trouble Codes from the vehicle"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot read DTCs - not connected")
            self.dtcCodesChanged.emit([])
            return

        logger.info("[OBD] Reading DTCs...")
        threading.Thread(target=self._do_read_dtc, daemon=True).start()

    def _do_read_dtc(self):
        """Read DTCs in background thread using a sync connection"""
        sync_conn = None
        use_diagnostic_conn = self._diagnostic_mode and self._diagnostic_sync_conn
        try:
            if use_diagnostic_conn:
                # Use the persistent diagnostic connection
                sync_conn = self._diagnostic_sync_conn
            else:
                # Stop async polling while we use the port
                self._connection.stop()
                # Create a fresh sync connection (like the working test script)
                sync_conn = self._create_sync_connection()

            if not sync_conn:
                logger.info("[OBD] Failed to get sync connection for DTC read")
                self._emitDtcCodes.emit([])
                return

            # Log connection status for debugging
            logger.info(f"[OBD] DTC read - sync connection status: {sync_conn.status()}")
            logger.info(f"[OBD] DTC read - sync connection port: {sync_conn.port_name()}")

            response = sync_conn.query(obd.commands.GET_DTC)
            logger.info(f"[OBD] DTC response: {response}")

            if response.is_null():
                logger.info("[OBD] No DTCs found (null response) - vehicle may not support GET_DTC or no codes stored")
                self._dtc_codes = []
                self._dtc_count = 0
                self._emitDtcCodes.emit([])
                self._emitDtcCount.emit(0)
                return

            # Response value is a list of tuples: [(code, description), ...]
            dtcs = response.value if response.value else []
            logger.info(f"[OBD] Raw DTC response value: {response.value}")
            # Convert to list of dicts for QML
            dtc_list = [{"code": code, "description": desc} for code, desc in dtcs]

            self._dtc_codes = dtc_list
            self._dtc_count = len(dtc_list)

            logger.info(f"[OBD] Found {len(dtc_list)} DTCs: {dtc_list}")

            # Emit signals via internal signals for thread-safe delivery to QML
            self._emitDtcCodes.emit(dtc_list)
            self._emitDtcCount.emit(len(dtc_list))

        except Exception as e:
            logger.error(f"[OBD] Error reading DTCs: {e}")
            self._emitDtcCodes.emit([])
        finally:
            if not use_diagnostic_conn:
                # Only close and resume if we created a temporary connection
                if sync_conn:
                    sync_conn.close()
                # Resume async polling
                if self._connection:
                    self._connection.start()

    @Slot()
    def read_current_dtc(self):
        """Read DTCs from the current/last driving cycle"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot read current DTCs - not connected")
            return

        logger.info("[OBD] Reading current DTCs...")
        threading.Thread(target=self._do_read_current_dtc, daemon=True).start()

    def _do_read_current_dtc(self):
        """Read current DTCs in background thread using a sync connection"""
        sync_conn = None
        use_diagnostic_conn = self._diagnostic_mode and self._diagnostic_sync_conn
        try:
            if use_diagnostic_conn:
                sync_conn = self._diagnostic_sync_conn
            else:
                # Stop async polling while we use the port
                self._connection.stop()
                sync_conn = self._create_sync_connection()

            if not sync_conn:
                logger.info("[OBD] Failed to get sync connection for current DTC read")
                self._emitDtcCodes.emit([])
                return

            response = sync_conn.query(obd.commands.GET_CURRENT_DTC)

            if response.is_null():
                logger.info("[OBD] No current DTCs found")
                self._emitDtcCodes.emit([])
                return

            dtcs = response.value if response.value else []
            dtc_list = [{"code": code, "description": desc} for code, desc in dtcs]

            logger.info(f"[OBD] Found {len(dtc_list)} current DTCs")
            codes = dtc_list
            self._emitDtcCodes.emit(codes)

        except Exception as e:
            logger.error(f"[OBD] Error reading current DTCs: {e}")
            self._emitDtcCodes.emit([])
        finally:
            if not use_diagnostic_conn:
                if sync_conn:
                    sync_conn.close()
                # Resume async polling
                if self._connection:
                    self._connection.start()

    @Slot()
    def read_freeze_frame(self):
        """Read freeze frame DTCs"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot read freeze frame - not connected")
            return

        logger.info("[OBD] Reading freeze frame DTCs...")
        threading.Thread(target=self._do_read_freeze_frame, daemon=True).start()

    def _do_read_freeze_frame(self):
        """Read freeze frame in background thread using a sync connection"""
        sync_conn = None
        use_diagnostic_conn = self._diagnostic_mode and self._diagnostic_sync_conn
        try:
            if use_diagnostic_conn:
                sync_conn = self._diagnostic_sync_conn
            else:
                # Stop async polling while we use the port
                self._connection.stop()
                sync_conn = self._create_sync_connection()

            if not sync_conn:
                logger.info("[OBD] Failed to get sync connection for freeze frame read")
                self._emitFreezeFrame.emit([])
                return

            response = sync_conn.query(obd.commands.FREEZE_DTC)

            if response.is_null():
                logger.info("[OBD] No freeze frame data")
                self._freeze_frame_dtcs = []
                self._emitFreezeFrame.emit([])
                return

            dtcs = response.value if response.value else []
            dtc_list = [{"code": code, "description": desc} for code, desc in dtcs]
            self._freeze_frame_dtcs = dtc_list

            logger.info(f"[OBD] Freeze frame DTCs: {dtc_list}")
            self._emitFreezeFrame.emit(dtc_list)

        except Exception as e:
            logger.error(f"[OBD] Error reading freeze frame: {e}")
            self._emitFreezeFrame.emit([])
        finally:
            if not use_diagnostic_conn:
                if sync_conn:
                    sync_conn.close()
                # Resume async polling
                if self._connection:
                    self._connection.start()

    @Slot()
    def clear_dtc(self):
        """Clear all DTCs and reset the MIL (Check Engine Light).
        WARNING: This will turn off the check engine light!"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot clear DTCs - not connected")
            self.dtcClearResult.emit(False, "Not connected to vehicle")
            return

        logger.info("[OBD] Clearing DTCs...")
        threading.Thread(target=self._do_clear_dtc, daemon=True).start()

    def _do_clear_dtc(self):
        """Clear DTCs in background thread using a sync connection"""
        sync_conn = None
        use_diagnostic_conn = self._diagnostic_mode and self._diagnostic_sync_conn
        try:
            if use_diagnostic_conn:
                sync_conn = self._diagnostic_sync_conn
            else:
                # Stop async polling while we use the port
                self._connection.stop()
                sync_conn = self._create_sync_connection()

            if not sync_conn:
                logger.info("[OBD] Failed to get sync connection for clear DTC")
                self._emitDtcClearResult.emit(False, "Connection failed")
                return

            response = sync_conn.query(obd.commands.CLEAR_DTC)

            if response.is_null():
                logger.info("[OBD] Clear DTC command returned null")
                self._emitDtcClearResult.emit(False, "Clear command failed")
                return

            # Clear successful
            self._dtc_codes = []
            self._dtc_count = 0
            self._mil_status = False

            logger.info("[OBD] DTCs cleared successfully")
            self._emitDtcClearResult.emit(True, "DTCs cleared successfully")
            self._emitDtcCodes.emit([])
            self._emitDtcCount.emit(0)
            self._emitMilStatus.emit(False)

        except Exception as e:
            logger.error(f"[OBD] Error clearing DTCs: {e}")
            self._emitDtcClearResult.emit(False, str(e))
        finally:
            if not use_diagnostic_conn:
                if sync_conn:
                    sync_conn.close()
                # Resume async polling
                if self._connection:
                    self._connection.start()

    @Slot()
    def read_status(self):
        """Read vehicle status including MIL and DTC count"""
        if not self._connection or not self._connected:
            logger.info("[OBD] Cannot read status - not connected")
            return

        logger.info("[OBD] Reading vehicle status...")
        threading.Thread(target=self._do_read_status, daemon=True).start()

    def _do_read_status(self):
        """Read status in background thread using a sync connection"""
        sync_conn = None
        use_diagnostic_conn = self._diagnostic_mode and self._diagnostic_sync_conn
        try:
            if use_diagnostic_conn:
                sync_conn = self._diagnostic_sync_conn
            else:
                # Stop async polling while we use the port
                self._connection.stop()
                sync_conn = self._create_sync_connection()

            if not sync_conn:
                logger.info("[OBD] Failed to get sync connection for status read")
                self._emitMilStatus.emit(False)
                self._emitDtcCount.emit(0)
                return

            response = sync_conn.query(obd.commands.STATUS)

            if response.is_null():
                logger.info("[OBD] Status command returned null")
                self._emitMilStatus.emit(False)
                self._emitDtcCount.emit(0)
                return

            status = response.value
            self._mil_status = status.MIL
            self._dtc_count = status.DTC_count

            logger.info(f"[OBD] MIL: {self._mil_status}, DTC count: {self._dtc_count}")

            # Emit signals via internal signals for thread-safe delivery to QML
            self._emitMilStatus.emit(self._mil_status)
            self._emitDtcCount.emit(self._dtc_count)

        except Exception as e:
            logger.error(f"[OBD] Error reading status: {e}")
            self._emitMilStatus.emit(False)
            self._emitDtcCount.emit(0)
        finally:
            if not use_diagnostic_conn:
                if sync_conn:
                    sync_conn.close()
                # Resume async polling
                if self._connection:
                    self._connection.start()

    # ==================== Thread-safe signal forwarding ====================
    # These slots receive signals from background threads and re-emit them on the main thread

    @Slot(list)
    def _forwardDtcCodes(self, codes):
        """Forward DTC codes signal to QML (runs on main thread)"""
        logger.info(f"[OBD] Forwarding dtcCodesChanged signal with {len(codes) if codes else 0} codes")
        self.dtcCodesChanged.emit(codes)

    @Slot(int)
    def _forwardDtcCount(self, count):
        """Forward DTC count signal to QML (runs on main thread)"""
        logger.info(f"[OBD] Forwarding dtcCountChanged signal with count: {count}")
        self.dtcCountChanged.emit(count)

    @Slot(bool)
    def _forwardMilStatus(self, status):
        """Forward MIL status signal to QML (runs on main thread)"""
        logger.info(f"[OBD] Forwarding milStatusChanged signal with status: {status}")
        self.milStatusChanged.emit(status)

    @Slot(bool, str)
    def _forwardDtcClearResult(self, success, message):
        """Forward DTC clear result signal to QML (runs on main thread)"""
        logger.info(f"[OBD] Forwarding dtcClearResult signal: {success}, {message}")
        self.dtcClearResult.emit(success, message)

    @Slot(list)
    def _forwardFreezeFrame(self, data):
        """Forward freeze frame signal to QML (runs on main thread)"""
        logger.info(f"[OBD] Forwarding freezeFrameChanged signal with {len(data) if data else 0} items")
        self.freezeFrameChanged.emit(data)

    # ==================== Diagnostic data getters ====================

    @Slot(result=list)
    def get_dtc_codes(self):
        """Get the last read DTCs"""
        return self._dtc_codes

    @Slot(result=int)
    def get_dtc_count(self):
        """Get the number of DTCs"""
        return self._dtc_count

    @Slot(result=bool)
    def get_mil_status(self):
        """Get MIL (Check Engine Light) status"""
        return self._mil_status

    @Slot(result=list)
    def get_freeze_frame(self):
        """Get freeze frame DTCs"""
        return self._freeze_frame_dtcs
