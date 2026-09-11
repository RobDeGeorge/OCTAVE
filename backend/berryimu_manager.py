"""
BerryIMU v3 Manager for OCTAVE
Reads accelerometer, gyroscope, magnetometer, and barometer data from BerryIMU v3
via I2C and emits signals to QML for the CarMenu 3D model and gauges.

Uses Madgwick AHRS for sensor fusion — outputs a quaternion directly
to the 3D model, completely avoiding gimbal lock. No Euler angle clamping,
no cascaded EMA stages. One filter, one quaternion, 1:1 mapping. Fusion is
deliberately accel+gyro only (6DOF); the magnetometer feeds the heading
readout only.
"""

import struct
import time
import math
import threading

from PySide6.QtCore import QObject, Signal, Slot, QTimer, Property

from backend.logging_config import get_logger
logger = get_logger(__name__)

# I2C Configuration
I2C_BUS = 2
LSM6DSL_ADDR = 0x6A
LIS3MDL_ADDR = 0x1C
BMP388_ADDR = 0x77

SEA_LEVEL_PRESSURE = 1013.25

# Madgwick filter gain — lower = smoother/less jitter, higher = more responsive
# 0.01 = very smooth (slow correction), 0.1 = snappy (more noise)
MADGWICK_BETA = 0.03

# Gyro deadband — zero out readings below this (deg/s) to prevent stationary drift
GYRO_DEADBAND = 0.4

# Signal emission interval — ~60Hz
EMIT_INTERVAL = 0.016
IDLE_EMIT_INTERVAL = 0.2   # 5 Hz while no page shows the values

# Sensor sample interval — ~200Hz target. The read loop sleeps for whatever is
# left of this period after the I2C reads, so it never busy-spins. Measured on
# the Orange Pi 5 Plus: the three I2C block reads cost ~4.3ms per iteration,
# so the loop is bus-bound and settles at ~172Hz with ~8% CPU on the worker
# thread — that CPU is the cost of reading the sensor, not a bug. Do NOT set
# this below the read cost (~0.005 here): `remaining` goes negative, the sleep
# is skipped, and the loop spins. Raising it to 0.01 (100Hz) roughly halves
# bus traffic and CPU while still giving the Madgwick filter ~1.7 samples per
# 60Hz emit; below ~100Hz the filter starts to starve.
SAMPLE_INTERVAL = 0.005

# Barometer read interval — ~5Hz, time-based so it doesn't drift with loop rate
BARO_INTERVAL = 0.2


# ==================== Madgwick AHRS Filter ====================

class MadgwickAHRS:
    """Madgwick MARG filter (full 9DOF implementation).

    The read loop deliberately calls update() with mx=my=mz=0 so only the
    6DOF accel+gyro branch runs — see the call site in _read_loop for why.
    """

    def __init__(self, beta=0.04):
        self.beta = beta
        # Quaternion: [w, x, y, z]
        self.q = [1.0, 0.0, 0.0, 0.0]

    def update(self, gx, gy, gz, ax, ay, az, mx, my, mz, dt):
        """Feed in gyro (rad/s), accel (g), mag (gauss), timestep (s)."""
        q0, q1, q2, q3 = self.q

        # Gyro quaternion rate of change
        qd0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz)
        qd1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy)
        qd2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx)
        qd3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx)

        # Normalize accelerometer
        a_norm = math.sqrt(ax * ax + ay * ay + az * az)
        if a_norm < 0.001:
            # No valid accel, just integrate gyro
            q0 += qd0 * dt; q1 += qd1 * dt; q2 += qd2 * dt; q3 += qd3 * dt
            n = math.sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3)
            self.q = [q0/n, q1/n, q2/n, q3/n]
            return
        ax /= a_norm; ay /= a_norm; az /= a_norm

        # Normalize magnetometer
        m_norm = math.sqrt(mx * mx + my * my + mz * mz)
        use_mag = m_norm > 0.001
        if use_mag:
            mx /= m_norm; my /= m_norm; mz /= m_norm

        # Precompute products
        q0q0 = q0 * q0; q0q1 = q0 * q1; q0q2 = q0 * q2; q0q3 = q0 * q3
        q1q1 = q1 * q1; q1q2 = q1 * q2; q1q3 = q1 * q3
        q2q2 = q2 * q2; q2q3 = q2 * q3; q3q3 = q3 * q3

        _2q0 = 2.0 * q0; _2q1 = 2.0 * q1; _2q2 = 2.0 * q2; _2q3 = 2.0 * q3
        _2q0q2 = 2.0 * q0q2; _2q2q3 = 2.0 * q2q3

        if use_mag:
            # Reference direction of Earth's magnetic field
            _2q0mx = _2q0 * mx; _2q0my = _2q0 * my; _2q0mz = _2q0 * mz
            _2q1mx = _2q1 * mx

            hx = (mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1
                  + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3)
            hy = (_2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2
                  - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3)
            _2bx = math.sqrt(hx * hx + hy * hy)
            _2bz = (-_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3
                    - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3)
            _4bx = 2.0 * _2bx
            _4bz = 2.0 * _2bz

            # Gradient descent corrective step (accel + mag)
            s0 = (-_2q2 * (2.0 * q1q3 - _2q0q2 - ax)
                  + _2q1 * (2.0 * q0q1 + _2q2q3 - ay)
                  - _2bz * q2 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx)
                  + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my)
                  + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz))

            s1 = (_2q3 * (2.0 * q1q3 - _2q0q2 - ax)
                  + _2q0 * (2.0 * q0q1 + _2q2q3 - ay)
                  - 4.0 * q1 * (1.0 - 2.0 * q1q1 - 2.0 * q2q2 - az)
                  + _2bz * q3 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx)
                  + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my)
                  + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz))

            s2 = (-_2q0 * (2.0 * q1q3 - _2q0q2 - ax)
                  + _2q3 * (2.0 * q0q1 + _2q2q3 - ay)
                  - 4.0 * q2 * (1.0 - 2.0 * q1q1 - 2.0 * q2q2 - az)
                  + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx)
                  + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my)
                  + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz))

            s3 = (_2q1 * (2.0 * q1q3 - _2q0q2 - ax)
                  + _2q2 * (2.0 * q0q1 + _2q2q3 - ay)
                  + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx)
                  + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my)
                  + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz))
        else:
            # 6DOF fallback — accel only correction (no heading)
            s0 = -_2q2 * (2.0 * q1q3 - _2q0q2 - ax) + _2q1 * (2.0 * q0q1 + _2q2q3 - ay)
            s1 = (_2q3 * (2.0 * q1q3 - _2q0q2 - ax) + _2q0 * (2.0 * q0q1 + _2q2q3 - ay)
                  - 4.0 * q1 * (1.0 - 2.0 * q1q1 - 2.0 * q2q2 - az))
            s2 = (-_2q0 * (2.0 * q1q3 - _2q0q2 - ax) + _2q3 * (2.0 * q0q1 + _2q2q3 - ay)
                  - 4.0 * q2 * (1.0 - 2.0 * q1q1 - 2.0 * q2q2 - az))
            s3 = _2q1 * (2.0 * q1q3 - _2q0q2 - ax) + _2q2 * (2.0 * q0q1 + _2q2q3 - ay)

        # Normalize gradient step
        s_norm = math.sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3)
        if s_norm > 0.0:
            s0 /= s_norm; s1 /= s_norm; s2 /= s_norm; s3 /= s_norm

        # Apply correction to gyro rate
        qd0 -= self.beta * s0
        qd1 -= self.beta * s1
        qd2 -= self.beta * s2
        qd3 -= self.beta * s3

        # Integrate
        q0 += qd0 * dt
        q1 += qd1 * dt
        q2 += qd2 * dt
        q3 += qd3 * dt

        # Normalize quaternion
        n = math.sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3)
        self.q = [q0 / n, q1 / n, q2 / n, q3 / n]

    def get_euler(self):
        """Extract pitch, roll, heading from quaternion (for gauge displays only).

        Currently unused: the read loop derives pitch/roll from the tared,
        yaw-stripped quaternion itself and heading from the raw magnetometer.
        Only meaningful for heading if 9DOF fusion is ever enabled.
        """
        w, x, y, z = self.q

        # Pitch (nose up/down)
        sinp = 2.0 * (w * y - z * x)
        if abs(sinp) >= 1.0:
            pitch = math.copysign(90.0, sinp)
        else:
            pitch = math.degrees(math.asin(sinp))

        # Roll (lean left/right)
        sinr = 2.0 * (w * x + y * z)
        cosr = 1.0 - 2.0 * (x * x + y * y)
        roll = math.degrees(math.atan2(sinr, cosr))

        # Heading / yaw
        siny = 2.0 * (w * z + x * y)
        cosy = 1.0 - 2.0 * (y * y + z * z)
        heading = math.degrees(math.atan2(siny, cosy))
        if heading < 0:
            heading += 360.0

        return pitch, roll, heading


# ==================== Manager ====================

class BerryIMUManager(QObject):
    """
    PySide6 manager that reads BerryIMU v3 sensors in a background thread
    and emits orientation as a quaternion (no gimbal lock) plus gauge values.
    """

    # Quaternion for 3D model (no gimbal lock)
    orientationChanged = Signal(float, float, float, float)  # w, x, y, z

    # Gauge display values
    pitchChanged = Signal(float)
    rollChanged = Signal(float)
    headingChanged = Signal(float)
    altitudeChanged = Signal(float)
    accelMagnitudeChanged = Signal(float)
    lateralGChanged = Signal(float)
    longitudinalGChanged = Signal(float)
    baroTempChanged = Signal(float)
    connectionStatusChanged = Signal(str)
    hasTemperatureChanged = Signal(bool)

    def __init__(self):
        super().__init__()
        self._running = False
        self._thread = None
        self._bus = None
        self._bmp_calib = {}
        self._shutting_down = False
        self._enabled = True

        # Reconnection state
        self._retry_count = 0
        self._max_retries = 5
        self._retry_delay_ms = 5000

        # Gyro bias (calibrated at startup)
        self._gyro_bias_x = 0.0
        self._gyro_bias_y = 0.0
        self._gyro_bias_z = 0.0

        # Tare offset quaternion — applied to zero out a reference orientation
        self._tare_q = [1.0, 0.0, 0.0, 0.0]  # identity = no offset

        # Accel bias for G-force zeroing (captured at tare time)
        self._accel_bias_x = 0.0
        self._accel_bias_y = 0.0
        self._last_ax = 0.0
        self._last_ay = 0.0

        # Tare requests are raised from the GUI thread and serviced inside the
        # read loop, so the quaternion / accel snapshot is taken on the thread
        # that writes them (mirrors the C++ worker's atomic request flags).
        self._tare_requested = threading.Event()
        self._tare_reset_requested = threading.Event()

        self._last_time = None
        self._ahrs = MadgwickAHRS(beta=MADGWICK_BETA)
        self._emit_interval = EMIT_INTERVAL
        self._active = True   # a sensor page is showing (Main.qml drives this)

        self._settings_manager = None

        # Start connection attempt after a short delay
        QTimer.singleShot(1000, self._start)

    def connect_settings_manager(self, settings_manager):
        self._settings_manager = settings_manager
        self._enabled = settings_manager.imuEnabled

    def _schedule_retry(self):
        """Schedule a reconnection attempt with linear backoff."""
        if self._shutting_down:
            return
        if self._retry_count >= self._max_retries:
            logger.warning(f"BerryIMU: max retries ({self._max_retries}) reached, giving up")
            return
        self._retry_count += 1
        delay = self._retry_delay_ms * self._retry_count
        logger.info(f"BerryIMU: retry {self._retry_count}/{self._max_retries} in {delay}ms")
        QTimer.singleShot(delay, self._start)

    def _cleanup_bus(self):
        """Close the I2C bus if open."""
        if self._bus:
            try:
                self._bus.close()
            except Exception:
                pass
            self._bus = None

    def _start(self):
        """Initialize sensors and start the read thread."""
        if self._shutting_down:
            return
        if not self._enabled:
            logger.info("BerryIMU: disabled in settings, skipping start")
            self.connectionStatusChanged.emit("Disabled")
            return

        # Clean up previous state before (re)connecting
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._cleanup_bus()

        try:
            import smbus2 as smbus
        except ImportError:
            logger.warning("BerryIMU: smbus2 not installed, IMU disabled")
            self.connectionStatusChanged.emit("Disconnected")
            return

        try:
            self._bus = smbus.SMBus(I2C_BUS)
            self._init_lsm6dsl()
            self._init_lis3mdl()
            self._init_bmp388()
            logger.info("BerryIMU: all sensors initialized successfully")
        except FileNotFoundError:
            logger.warning(f"BerryIMU: /dev/i2c-{I2C_BUS} not found, IMU disabled")
            self.connectionStatusChanged.emit("Disconnected")
            self._cleanup_bus()
            self._schedule_retry()
            return
        except PermissionError:
            logger.warning(f"BerryIMU: permission denied on /dev/i2c-{I2C_BUS}")
            self.connectionStatusChanged.emit("Disconnected")
            self._cleanup_bus()
            return
        except Exception as e:
            logger.error(f"BerryIMU: sensor init failed: {e}")
            self.connectionStatusChanged.emit("Error")
            self._cleanup_bus()
            self._schedule_retry()
            return

        self._retry_count = 0
        self._running = True
        self.connectionStatusChanged.emit("Connected")
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    # ==================== Sensor Init ====================

    def _init_lsm6dsl(self):
        """Init accelerometer + gyroscope at 416Hz."""
        self._bus.write_byte_data(LSM6DSL_ADDR, 0x10, 0x68)  # Accel: 416Hz, +/-4g
        self._bus.write_byte_data(LSM6DSL_ADDR, 0x11, 0x60)  # Gyro: 416Hz, 245dps
        self._bus.write_byte_data(LSM6DSL_ADDR, 0x12, 0x44)  # BDU + auto-inc

    def _init_lis3mdl(self):
        """Init magnetometer at 155Hz."""
        self._bus.write_byte_data(LIS3MDL_ADDR, 0x20, 0xFE)  # Ultra-high perf, 155Hz
        self._bus.write_byte_data(LIS3MDL_ADDR, 0x21, 0x20)  # +/-8 gauss
        self._bus.write_byte_data(LIS3MDL_ADDR, 0x22, 0x00)  # Continuous
        self._bus.write_byte_data(LIS3MDL_ADDR, 0x23, 0x0C)  # Ultra-high Z

    def _init_bmp388(self):
        """Init barometer and read calibration."""
        raw = self._bus.read_i2c_block_data(BMP388_ADDR, 0x31, 21)
        self._bmp_calib['T1'] = struct.unpack('<H', bytes(raw[0:2]))[0]
        self._bmp_calib['T2'] = struct.unpack('<H', bytes(raw[2:4]))[0]
        self._bmp_calib['T3'] = struct.unpack('<b', bytes([raw[4]]))[0]
        self._bmp_calib['P1'] = struct.unpack('<h', bytes(raw[5:7]))[0]
        self._bmp_calib['P2'] = struct.unpack('<h', bytes(raw[7:9]))[0]
        self._bmp_calib['P3'] = struct.unpack('<b', bytes([raw[9]]))[0]
        self._bmp_calib['P4'] = struct.unpack('<b', bytes([raw[10]]))[0]
        self._bmp_calib['P5'] = struct.unpack('<H', bytes(raw[11:13]))[0]
        self._bmp_calib['P6'] = struct.unpack('<H', bytes(raw[13:15]))[0]
        self._bmp_calib['P7'] = struct.unpack('<b', bytes([raw[15]]))[0]
        self._bmp_calib['P8'] = struct.unpack('<b', bytes([raw[16]]))[0]
        self._bmp_calib['P9'] = struct.unpack('<h', bytes(raw[17:19]))[0]
        self._bmp_calib['P10'] = struct.unpack('<b', bytes([raw[19]]))[0]
        self._bmp_calib['P11'] = struct.unpack('<b', bytes([raw[20]]))[0]
        self._bus.write_byte_data(BMP388_ADDR, 0x1C, 0x03)
        self._bus.write_byte_data(BMP388_ADDR, 0x1D, 0x02)
        self._bus.write_byte_data(BMP388_ADDR, 0x1B, 0x33)
        time.sleep(0.05)

    # ==================== Sensor Read ====================

    def _read_accel(self):
        data = self._bus.read_i2c_block_data(LSM6DSL_ADDR, 0x28, 6)
        x = struct.unpack('<h', bytes(data[0:2]))[0] * 0.000122
        y = struct.unpack('<h', bytes(data[2:4]))[0] * 0.000122
        z = struct.unpack('<h', bytes(data[4:6]))[0] * 0.000122
        return x, y, z

    def _read_gyro(self):
        data = self._bus.read_i2c_block_data(LSM6DSL_ADDR, 0x22, 6)
        x = struct.unpack('<h', bytes(data[0:2]))[0] * 0.00875
        y = struct.unpack('<h', bytes(data[2:4]))[0] * 0.00875
        z = struct.unpack('<h', bytes(data[4:6]))[0] * 0.00875
        return x, y, z

    def _read_mag(self):
        data = self._bus.read_i2c_block_data(LIS3MDL_ADDR, 0x28 | 0x80, 6)
        x = struct.unpack('<h', bytes(data[0:2]))[0] / 3421.0
        y = struct.unpack('<h', bytes(data[2:4]))[0] / 3421.0
        z = struct.unpack('<h', bytes(data[4:6]))[0] / 3421.0
        return x, y, z

    def _read_baro(self):
        data = self._bus.read_i2c_block_data(BMP388_ADDR, 0x04, 6)
        raw_press = data[0] | (data[1] << 8) | (data[2] << 16)
        raw_temp = data[3] | (data[4] << 8) | (data[5] << 16)
        c = self._bmp_calib
        partial1 = raw_temp - (c['T1'] * 256.0)
        partial2 = c['T2'] / 1073741824.0
        partial3 = partial1 * partial2
        partial4 = partial1 * partial1
        partial5 = partial4 * (c['T3'] / 281474976710656.0)
        comp_temp = partial3 + partial5
        t = comp_temp
        pp1 = (c['P1'] - 16384.0) / 1048576.0
        pp2 = (c['P2'] - 16384.0) / 536870912.0
        pp3 = c['P3'] / 4294967296.0
        pp4 = c['P4'] / 137438953472.0
        pp5 = c['P5'] / 0.125
        pp6 = c['P6'] / 64.0
        pp7 = c['P7'] / 256.0
        pp8 = c['P8'] / 32768.0
        pp9 = c['P9'] / 281474976710656.0
        pp10 = c['P10'] / 281474976710656.0
        pp11 = c['P11'] / 36893488147419103232.0
        out1 = pp5 + pp6 * t + pp7 * t * t + pp8 * t * t * t
        out2 = raw_press * (pp1 + pp2 * t + pp3 * t * t + pp4 * t * t * t)
        out3 = raw_press * raw_press * (pp9 + pp10 * t) + raw_press * raw_press * raw_press * pp11
        comp_press = out1 + out2 + out3
        pressure_hpa = comp_press / 100.0
        altitude = 44330.0 * (1.0 - (pressure_hpa / SEA_LEVEL_PRESSURE) ** (1.0 / 5.255))
        return pressure_hpa, comp_temp, altitude

    # ==================== Gyro Calibration ====================

    def _calibrate_gyro(self):
        """Sample gyro for ~1s while stationary to determine bias."""
        N = 200
        sx, sy, sz = 0.0, 0.0, 0.0
        for _ in range(N):
            gx, gy, gz = self._read_gyro()
            sx += gx; sy += gy; sz += gz
            time.sleep(0.005)
        self._gyro_bias_x = sx / N
        self._gyro_bias_y = sy / N
        self._gyro_bias_z = sz / N
        logger.info(f"BerryIMU: gyro calibrated — bias x={self._gyro_bias_x:.4f} "
                    f"y={self._gyro_bias_y:.4f} z={self._gyro_bias_z:.4f} dps")

    # ==================== Read Loop ====================

    def _read_loop(self):
        """Read sensors at ~200Hz, feed Madgwick filter, emit quaternion at ~60Hz."""

        self._calibrate_gyro()
        self._last_time = time.monotonic()
        last_emit = 0.0
        last_baro = 0.0      # 0 => first baro read happens on the first iteration
        baro_logged = False
        read_count = 0

        while self._running:
            try:
                now = time.monotonic()
                dt = now - self._last_time
                self._last_time = now
                if dt <= 0:
                    dt = 0.001

                # Handle tare requests (raised by calibrateTare / resetTare)
                if self._tare_requested.is_set():
                    self._tare_requested.clear()
                    self._apply_tare()
                if self._tare_reset_requested.is_set():
                    self._tare_reset_requested.clear()
                    self._apply_tare_reset()

                # Read all sensors
                ax, ay, az = self._read_accel()
                gx, gy, gz = self._read_gyro()
                mx, my, mz = self._read_mag()

                # Remove gyro bias and apply deadband
                gx_corr = gx - self._gyro_bias_x
                gy_corr = gy - self._gyro_bias_y
                gz_corr = gz - self._gyro_bias_z
                if abs(gx_corr) < GYRO_DEADBAND:
                    gx_corr = 0.0
                if abs(gy_corr) < GYRO_DEADBAND:
                    gy_corr = 0.0
                if abs(gz_corr) < GYRO_DEADBAND:
                    gz_corr = 0.0
                gx_rad = math.radians(gx_corr)
                gy_rad = math.radians(gy_corr)
                gz_rad = math.radians(gz_corr)

                # Only update filter when there's actual motion — freezes quaternion
                # when stationary so the accel correction doesn't cause visible settling.
                # Fusion is deliberately accel+gyro only (6DOF): the magnetometer is
                # passed as zeros so MadgwickAHRS takes its 6DOF branch. In-car
                # magnetic interference (alternator, speakers, steel body) would pull
                # the quaternion around if it were fused; the magnetometer is used
                # only for the raw heading readout below. Enabling 9DOF fusion is a
                # behaviour change that needs on-vehicle testing first.
                if gx_corr != 0.0 or gy_corr != 0.0 or gz_corr != 0.0:
                    self._ahrs.update(gx_rad, gy_rad, gz_rad, ax, ay, az, 0.0, 0.0, 0.0, dt)

                # G-force — store latest for tare capture
                self._last_ax = ax
                self._last_ay = ay
                accel_mag = math.sqrt(ax * ax + ay * ay + az * az)

                # Emit at ~60Hz
                read_count += 1
                # Sampling and fusion continue at full rate; only the emission
                # into QML slows down while nothing on screen consumes it.
                emit_interval = self._emit_interval if self._active else max(self._emit_interval, IDLE_EMIT_INTERVAL)
                if now - last_emit >= emit_interval:
                    last_emit = now

                    # Strip yaw from quaternion — only keep pitch & roll
                    # Yaw = rotation around sensor Z axis, decompose: q = q_yaw * q_tilt
                    # We want q_tilt = q_yaw_inverse * q
                    qw, qx, qy, qz = self._ahrs.q
                    yaw_norm = math.sqrt(qw * qw + qz * qz)
                    if yaw_norm > 0.001:
                        yw = qw / yaw_norm
                        yz = qz / yaw_norm
                    else:
                        yw = 1.0
                        yz = 0.0
                    # q_yaw_inv * q (quaternion multiply with [yw, 0, 0, -yz])
                    tw = yw * qw + yz * qz
                    tx = yw * qx + yz * qy
                    ty = yw * qy - yz * qx
                    tz = yw * qz - yz * qw

                    # Apply tare offset: q_out = q_tare * q_current
                    # This zeros out the orientation captured at tare time
                    tqw, tqx, tqy, tqz = self._tare_q
                    ow = tqw*tw - tqx*tx - tqy*ty - tqz*tz
                    ox = tqw*tx + tqx*tw + tqy*tz - tqz*ty
                    oy = tqw*ty - tqx*tz + tqy*tw + tqz*tx
                    oz = tqw*tz + tqx*ty - tqy*tx + tqz*tw
                    self.orientationChanged.emit(ow, ox, oy, oz)

                    # Euler angles for gauges — also apply tare
                    # Extract pitch/roll from the tared quaternion
                    sinp = 2.0 * (ow * oy - oz * ox)
                    if abs(sinp) >= 1.0:
                        pitch = math.copysign(90.0, sinp)
                    else:
                        pitch = math.degrees(math.asin(sinp))
                    sinr = 2.0 * (ow * ox + oy * oz)
                    cosr = 1.0 - 2.0 * (ox * ox + oy * oy)
                    roll = math.degrees(math.atan2(sinr, cosr))
                    self.pitchChanged.emit(pitch)
                    self.rollChanged.emit(roll)

                    # Heading from raw magnetometer (not from filter — avoids drift)
                    heading = math.degrees(math.atan2(my, mx))
                    if heading < 0:
                        heading += 360.0
                    self.headingChanged.emit(heading)
                    self.accelMagnitudeChanged.emit(accel_mag)
                    self.lateralGChanged.emit(ay - self._accel_bias_y)
                    self.longitudinalGChanged.emit(ax - self._accel_bias_x)

                    if read_count == 1:
                        logger.info(
                            f"BerryIMU: first emit — pitch={pitch:.1f} roll={roll:.1f} "
                            f"heading={heading:.1f} g={accel_mag:.2f} "
                            f"q=[{tw:.3f},{tx:.3f},{ty:.3f},{tz:.3f}]"
                        )

                # Baro at ~5Hz
                if now - last_baro >= BARO_INTERVAL:
                    last_baro = now
                    try:
                        _, baro_temp, altitude = self._read_baro()
                        self.altitudeChanged.emit(altitude)
                        self.baroTempChanged.emit(baro_temp)
                        if not baro_logged:
                            baro_logged = True
                            logger.info(f"BerryIMU: baro — alt={altitude:.1f}m temp={baro_temp:.1f}C")
                    except Exception as e:
                        logger.debug(f"BerryIMU: baro read error: {e}")

                # Sleep out the rest of the sample period (never spin)
                remaining = SAMPLE_INTERVAL - (time.monotonic() - now)
                if remaining > 0:
                    time.sleep(remaining)

            except Exception as e:
                logger.error(f"BerryIMU: read loop error: {e}")
                self._running = False
                self.connectionStatusChanged.emit("Error")
                self._cleanup_bus()
                self._schedule_retry()
                break

        logger.info(f"BerryIMU: read loop exited after {read_count} reads")

    # ==================== Tare (read-thread side) ====================

    def _apply_tare(self):
        """Capture the current orientation as the zero reference. Runs on the
        read thread so _ahrs.q and _last_ax/_last_ay are read where they're
        written."""
        # Strip yaw first (same decomposition as the emit path) so the tare
        # operates in the same domain as the values it's applied to.
        qw, qx, qy, qz = self._ahrs.q
        yaw_norm = math.sqrt(qw * qw + qz * qz)
        if yaw_norm > 0.001:
            yw = qw / yaw_norm
            yz = qz / yaw_norm
        else:
            yw = 1.0
            yz = 0.0
        tw = yw * qw + yz * qz
        tx = yw * qx + yz * qy
        ty = yw * qy - yz * qx
        tz = yw * qz - yz * qw
        # Inverse of a unit quaternion is its conjugate: (w, -x, -y, -z)
        self._tare_q = [tw, -tx, -ty, -tz]
        # Also capture current accel as G-force zero reference
        self._accel_bias_x = self._last_ax
        self._accel_bias_y = self._last_ay
        logger.info(f"BerryIMU: tare set — q=[{tw:.3f},{tx:.3f},{ty:.3f},{tz:.3f}] "
                    f"accel_bias=[{self._accel_bias_x:.4f},{self._accel_bias_y:.4f}]")

    def _apply_tare_reset(self):
        """Clear the tare offset (identity quaternion, zero accel bias)."""
        self._tare_q = [1.0, 0.0, 0.0, 0.0]
        self._accel_bias_x = 0.0
        self._accel_bias_y = 0.0
        logger.info("BerryIMU: tare reset to identity")

    # ==================== Properties ====================

    @Property(bool, notify=connectionStatusChanged)
    def connected(self):
        return self._running

    @Property(bool, notify=hasTemperatureChanged)
    def hasTemperature(self):
        # Desktop BerryIMU always carries the BMP388 barometer/thermometer.
        # Mirrors the C++ desktop branch (`return true`); the Android build
        # flips this at runtime from Qt sensors, which Python never targets.
        return True

    @Slot(result=str)
    def getConnectionStatus(self):
        return "Connected" if self._running else "Disconnected"

    @Slot(bool)
    def setEnabled(self, enabled):
        if enabled == self._enabled:
            return
        self._enabled = enabled
        if self._settings_manager:
            self._settings_manager.save_imu_enabled(enabled)
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

    @Slot(result=bool)
    def isEnabled(self):
        return self._enabled

    @Slot(int)
    def setEmitRate(self, hz):
        """Set the signal emission rate in Hz (10-120)."""
        hz = max(10, min(120, hz))
        self._emit_interval = 1.0 / hz
        logger.info(f"BerryIMU: emit rate set to {hz}Hz (interval={self._emit_interval:.4f}s)")

    @Slot(bool)
    def setActive(self, active):
        """Called by Main.qml on page changes: True while a sensor page is showing.
        Off-page the values are still fused but emitted at IDLE_EMIT_INTERVAL."""
        active = bool(active)
        if active == self._active:
            return
        self._active = active
        logger.debug("BerryIMU: %s", "sensor page shown: full emit rate" if active else "no sensor page: idle emit rate")

    @Slot()
    def calibrateTare(self):
        """Set current orientation as the zero reference (tare).

        Only raises a request; the read loop performs the capture on its own
        thread (same as the C++ worker) so the quaternion is not read from
        the GUI thread while the loop is writing it."""
        if not self._running:
            logger.info("BerryIMU: tare requested while not running, ignored")
            return
        self._tare_requested.set()

    @Slot()
    def resetTare(self):
        """Clear the tare offset."""
        if self._running:
            self._tare_reset_requested.set()
        else:
            self._apply_tare_reset()

    # ==================== Cleanup ====================

    @Slot()
    def cleanup(self):
        logger.info("BerryIMU: cleaning up...")
        self._shutting_down = True
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._cleanup_bus()
