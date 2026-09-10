/*
 * OCTAVE OBD-II BLE bridge.
 *
 * Native Android BluetoothGatt path that bypasses Qt 6.7's QLowEnergyController.
 * Qt's BLE stack on Android 16 (SDK 36) has a broken QBluetoothPermission
 * aggregator that returns Denied even when each underlying permission is
 * granted, which prevents QLowEnergyController from initializing the BLE
 * adapter. Going through Android's native API directly avoids the bug.
 *
 * Connection flow (NEXAS / Veepeak / Vgate BLE / BAFX adapters):
 *   1. BluetoothAdapter.getRemoteDevice(mac) — synchronous, no permission gate
 *   2. device.connectGatt(ctx, false, callback)
 *   3. onConnectionStateChange(STATE_CONNECTED) → gatt.discoverServices()
 *   4. onServicesDiscovered → find service FFE0 → find characteristic FFE1
 *   5. setCharacteristicNotification(FFE1, true) and write 0x0100 to FFE1's
 *      CCCD descriptor to enable notifications
 *   6. State flips to READY; C++ side starts ELM327 init via writeCommand()
 *   7. onCharacteristicChanged(FFE1) enqueues incoming bytes for C++ poll
 *
 * Threading: BluetoothGatt callbacks run on a Binder thread. The C++ side
 * polls pollResponse() and reads stateCode() from any thread; both are
 * thread-safe (volatile + ConcurrentLinkedQueue).
 */
package org.octave.app;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ConcurrentLinkedQueue;

public class OctaveOBDBridge {
    private static final String TAG = "OctaveOBDBridge";

    // ELM327 BLE adapters use one of three vendor profiles depending on the
    // Bluetooth chipset/firmware they shipped with. We try them in priority
    // order at service-discovery time. Adapters that advertise multiple are
    // accepted in priority order (ISSC > FFF0 > FFE0) since ISSC's
    // characteristics support higher MTU and are the modern standard.
    //
    //  1. Microchip ISSC Transparent UART (NEXAS, Vgate iCar Pro, recent
    //     Veepeak BLE+, OBDLink CX): two distinct characteristics, one for
    //     notify (RX), one for write (TX).
    //  2. FFF0 (Konnwei, some BAFX, generic clones): also two chars (FFF1
    //     notify, FFF2 write).
    //  3. FFE0/FFE1 (older Konnwei, HM-10 modules, classic clones): single
    //     characteristic that's both writable and notifying.
    private static final UUID CCCD_DESC      = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB");

    // ISSC Transparent UART
    private static final UUID ISSC_SERVICE   = UUID.fromString("49535343-FE7D-4AE5-8FA9-9FAFD205E455");
    private static final UUID ISSC_NOTIFY    = UUID.fromString("49535343-1E4D-4BD9-BA61-23C647249616");
    private static final UUID ISSC_WRITE     = UUID.fromString("49535343-8841-43F4-A8D4-ECBE34729BB3");

    // FFF0 profile
    private static final UUID FFF0_SERVICE   = UUID.fromString("0000FFF0-0000-1000-8000-00805F9B34FB");
    private static final UUID FFF1_CHAR      = UUID.fromString("0000FFF1-0000-1000-8000-00805F9B34FB");
    private static final UUID FFF2_CHAR      = UUID.fromString("0000FFF2-0000-1000-8000-00805F9B34FB");

    // FFE0 profile (single characteristic for both directions)
    private static final UUID FFE0_SERVICE   = UUID.fromString("0000FFE0-0000-1000-8000-00805F9B34FB");
    private static final UUID FFE1_CHAR      = UUID.fromString("0000FFE1-0000-1000-8000-00805F9B34FB");

    // State enum mirrored to C++. Not using a Java enum so JNI sees plain ints.
    public static final int STATE_DISCONNECTED       = 0;
    public static final int STATE_CONNECTING         = 1;
    public static final int STATE_DISCOVERING        = 2;
    public static final int STATE_READY              = 3;
    public static final int STATE_ERROR              = 4;

    private static volatile int currentState = STATE_DISCONNECTED;
    private static volatile String lastError = "";
    private static volatile String lastEvent = "";

    private static BluetoothGatt currentGatt = null;
    // For two-characteristic profiles (ISSC, FFF0) writeChar != notifyChar.
    // For single-char FFE0/FFE1 profile, both point at the same characteristic.
    private static BluetoothGattCharacteristic notifyChar = null;
    private static BluetoothGattCharacteristic writeChar = null;
    private static String activeProfileName = "";

    // Bytes received from FFE1 notifications. C++ polls this on a timer.
    // ConcurrentLinkedQueue is thread-safe for offer (Binder thread) +
    // poll (Qt main thread).
    private static final ConcurrentLinkedQueue<byte[]> pendingResponses =
        new ConcurrentLinkedQueue<>();

    // Every setEvent/setError line, queued for OCTAVE's own log files
    // (logcat alone is unreachable from the vehicle). C++ drains it on the
    // same 50 ms poll as pendingResponses; bounded so a disconnected
    // adapter cannot grow it without bound. Entries are "I|msg" / "W|msg".
    private static final ConcurrentLinkedQueue<String> pendingLog =
        new ConcurrentLinkedQueue<>();
    private static final int LOG_QUEUE_MAX = 200;

    /** Returns the current connection state (one of STATE_* constants). */
    public static int stateCode() { return currentState; }

    /** Returns the most-recent error/event string. C++ polls this for log feed. */
    public static String lastError() { return lastError; }
    public static String lastEvent() { return lastEvent; }

    /** Drain one pending response chunk from the FFE1 notification queue.
     *  Returns null when empty. C++ polls this on a 50ms timer. */
    public static byte[] pollResponse() { return pendingResponses.poll(); }

    /** Drain one queued log line ("I|..." or "W|..."); null when empty. */
    public static String pollLog() { return pendingLog.poll(); }

    /**
     * Connect to a BLE ELM327 adapter by MAC address.
     * Returns true if the connectGatt() call was issued (state will then
     * transition to STATE_CONNECTING and signal completion via stateCode()).
     * Returns false on hard failures (no adapter, bad MAC, no context).
     */
    public static synchronized boolean connect(Context ctx, String macAddress) {
        if (ctx == null) {
            setError("connect: null context");
            return false;
        }

        // On API 31+ Android requires runtime BLUETOOTH_CONNECT and
        // BLUETOOTH_SCAN grants for any Gatt operation. Fresh installs
        // (e.g. CI APKs the user just sideloaded) don't have them yet —
        // pop the system dialog from the Activity before we call connectGatt
        // and bail with a friendly error so the next tap succeeds.
        if (Build.VERSION.SDK_INT >= 31) {
            List<String> missing = new ArrayList<>();
            if (ctx.checkSelfPermission("android.permission.BLUETOOTH_CONNECT")
                    != PackageManager.PERMISSION_GRANTED) {
                missing.add("android.permission.BLUETOOTH_CONNECT");
            }
            if (ctx.checkSelfPermission("android.permission.BLUETOOTH_SCAN")
                    != PackageManager.PERMISSION_GRANTED) {
                missing.add("android.permission.BLUETOOTH_SCAN");
            }
            if (!missing.isEmpty()) {
                if (ctx instanceof Activity) {
                    setError("Requesting Bluetooth permission — tap Allow, then tap Connect again");
                    try {
                        ((Activity) ctx).requestPermissions(
                            missing.toArray(new String[0]), 7301);
                    } catch (Throwable t) {
                        setError("requestPermissions failed: " + t.getMessage());
                    }
                } else {
                    setError("Bluetooth permission missing and no Activity to request from");
                }
                currentState = STATE_ERROR;
                return false;
            }
        }

        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        if (bm == null) {
            setError("connect: no BluetoothManager service");
            return false;
        }
        BluetoothAdapter adapter = bm.getAdapter();
        if (adapter == null) {
            setError("connect: no BluetoothAdapter (BLE unsupported?)");
            return false;
        }
        if (!adapter.isEnabled()) {
            setError("connect: Bluetooth is disabled on the phone");
            return false;
        }
        if (macAddress == null || !BluetoothAdapter.checkBluetoothAddress(macAddress)) {
            setError("connect: invalid MAC address: " + macAddress);
            return false;
        }

        // Tear down any previous session before starting a new one.
        disconnect();

        BluetoothDevice device;
        try {
            device = adapter.getRemoteDevice(macAddress);
        } catch (IllegalArgumentException e) {
            setError("connect: getRemoteDevice failed: " + e.getMessage());
            return false;
        }

        currentState = STATE_CONNECTING;
        setEvent("connectGatt(" + macAddress + ")");
        try {
            // autoConnect=false → direct connect attempt with ~30s timeout.
            // TRANSPORT_LE forces BLE (some dual-mode adapters expose Classic SPP
            // too; we want LE).
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                currentGatt = device.connectGatt(ctx, false, gattCallback,
                    BluetoothDevice.TRANSPORT_LE);
            } else {
                currentGatt = device.connectGatt(ctx, false, gattCallback);
            }
            return currentGatt != null;
        } catch (SecurityException e) {
            // BLUETOOTH_CONNECT runtime permission missing or revoked.
            setError("connectGatt: SecurityException: " + e.getMessage());
            currentState = STATE_ERROR;
            return false;
        }
    }

    public static synchronized void disconnect() {
        if (currentGatt != null) {
            try {
                currentGatt.disconnect();
                currentGatt.close();
            } catch (SecurityException ignored) { /* ok */ }
            currentGatt = null;
        }
        notifyChar = null;
        writeChar = null;
        activeProfileName = "";
        pendingResponses.clear();
        currentState = STATE_DISCONNECTED;
    }

    /**
     * Write bytes to FFE1. Caller must chunk to <=20 bytes (default BLE MTU).
     * Returns true if the writeCharacteristic() call was issued.
     */
    public static synchronized boolean write(byte[] data) {
        if (currentGatt == null || writeChar == null) {
            setError("write: not connected (state=" + currentState + ")");
            return false;
        }
        try {
            writeChar.setValue(data);
            // Use NO_RESPONSE for single-char FFE0/FFE1 (less round-trip
            // overhead, fine for bulk ELM327 chatter). For two-char profiles
            // the write characteristic usually has WRITE only, so default to
            // WITH_RESPONSE which is more compatible.
            int props = writeChar.getProperties();
            if ((props & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) {
                writeChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
            } else {
                writeChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            }
            return currentGatt.writeCharacteristic(writeChar);
        } catch (SecurityException e) {
            setError("write: SecurityException: " + e.getMessage());
            return false;
        }
    }

    private static void setError(String msg) {
        Log.w(TAG, "ERROR: " + msg);
        lastError = msg;
        queueLog("W|" + msg);
    }
    private static void setEvent(String msg) {
        Log.i(TAG, msg);
        lastEvent = msg;
        queueLog("I|" + msg);
    }
    private static void queueLog(String line) {
        if (pendingLog.size() >= LOG_QUEUE_MAX) {
            pendingLog.poll();
        }
        pendingLog.offer(line);
    }

    private static final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                setEvent("GATT connected (status=" + status + ")");
                currentState = STATE_DISCOVERING;
                try {
                    gatt.discoverServices();
                } catch (SecurityException e) {
                    setError("discoverServices: SecurityException: " + e.getMessage());
                    currentState = STATE_ERROR;
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                setEvent("GATT disconnected (status=" + status + ")");
                currentState = STATE_DISCONNECTED;
                try { gatt.close(); } catch (SecurityException ignored) {}
                if (gatt == currentGatt) {
                    currentGatt = null;
                    notifyChar = null;
                    writeChar = null;
                    activeProfileName = "";
                }
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                setError("onServicesDiscovered failed (status=" + status + ")");
                currentState = STATE_ERROR;
                return;
            }

            // Try each known ELM327 BLE profile in priority order. First one
            // that actually has both notify + write characteristics wins.
            BluetoothGattService svc;

            // 1) ISSC Transparent UART (NEXAS, modern Veepeak BLE+, OBDLink CX)
            if ((svc = gatt.getService(ISSC_SERVICE)) != null) {
                BluetoothGattCharacteristic n = svc.getCharacteristic(ISSC_NOTIFY);
                BluetoothGattCharacteristic w = svc.getCharacteristic(ISSC_WRITE);
                if (n != null && w != null) {
                    if (configureProfile(gatt, n, w, "ISSC")) return;
                }
            }
            // 2) FFF0 / FFF1+FFF2 (Konnwei, some BAFX)
            if ((svc = gatt.getService(FFF0_SERVICE)) != null) {
                BluetoothGattCharacteristic n = svc.getCharacteristic(FFF1_CHAR);
                BluetoothGattCharacteristic w = svc.getCharacteristic(FFF2_CHAR);
                if (n != null && w != null) {
                    if (configureProfile(gatt, n, w, "FFF0/FFF1+FFF2")) return;
                }
                // FFF0 sometimes ships as a single FFF1 read+notify+write char.
                if (n != null && (n.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE) != 0) {
                    if (configureProfile(gatt, n, n, "FFF0/FFF1-single")) return;
                }
            }
            // 3) FFE0/FFE1 (older HM-10 modules, classic clones)
            if ((svc = gatt.getService(FFE0_SERVICE)) != null) {
                BluetoothGattCharacteristic c = svc.getCharacteristic(FFE1_CHAR);
                if (c != null) {
                    if (configureProfile(gatt, c, c, "FFE0/FFE1")) return;
                }
            }

            // No matching profile found — dump everything for diagnosis.
            StringBuilder sb = new StringBuilder("No known ELM327 profile. Services: ");
            for (BluetoothGattService s : gatt.getServices()) {
                sb.append(s.getUuid().toString()).append(" ");
            }
            setError(sb.toString());
            currentState = STATE_ERROR;
        }

        // Wires up `n` (notify char) and `w` (write char) and enables
        // notifications via CCCD. Returns true on success, false on failure.
        private boolean configureProfile(BluetoothGatt gatt,
                                         BluetoothGattCharacteristic n,
                                         BluetoothGattCharacteristic w,
                                         String profileName) {
            try {
                if (!gatt.setCharacteristicNotification(n, true)) {
                    setError("[" + profileName + "] setCharacteristicNotification failed");
                    return false;
                }
                BluetoothGattDescriptor cccd = n.getDescriptor(CCCD_DESC);
                if (cccd != null) {
                    cccd.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                    if (!gatt.writeDescriptor(cccd)) {
                        setError("[" + profileName + "] writeDescriptor(CCCD) failed");
                        // not fatal — try anyway
                    }
                } else {
                    setEvent("[" + profileName + "] CCCD missing — using char-only notify");
                }
            } catch (SecurityException e) {
                setError("[" + profileName + "] notify-enable: SecurityException: " + e.getMessage());
                return false;
            }
            notifyChar = n;
            writeChar = w;
            activeProfileName = profileName;
            setEvent("ready (profile=" + profileName + ")");
            currentState = STATE_READY;
            return true;
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic chr) {
            if (notifyChar == null || !chr.getUuid().equals(notifyChar.getUuid())) return;
            byte[] v = chr.getValue();
            if (v != null && v.length > 0) {
                pendingResponses.offer(v.clone());
            }
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor d, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                setError("onDescriptorWrite failed (status=" + status + ")");
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic chr, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                setError("onCharacteristicWrite failed (status=" + status + ")");
            }
        }
    };
}
