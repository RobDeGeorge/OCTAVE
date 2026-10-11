package org.octave.phoneserver.wrappers;

import org.octave.phoneserver.AndroidVersions;
import org.octave.phoneserver.util.Ln;

import android.os.Build;
import android.os.IInterface;

import java.lang.reflect.Method;

public final class PowerManager {
    private final IInterface manager;
    private Method isScreenOnMethod;
    private Method userActivityMethod;
    private boolean userActivityUnavailable;

    static PowerManager create() {
        IInterface manager = ServiceManager.getService("power", "android.os.IPowerManager");
        return new PowerManager(manager);
    }

    private PowerManager(IInterface manager) {
        this.manager = manager;
    }

    private Method getIsScreenOnMethod() throws NoSuchMethodException {
        if (isScreenOnMethod == null) {
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_34_ANDROID_14) {
                isScreenOnMethod = manager.getClass().getMethod("isDisplayInteractive", int.class);
            } else {
                isScreenOnMethod = manager.getClass().getMethod("isInteractive");
            }
        }
        return isScreenOnMethod;
    }

    public boolean isScreenOn(int displayId) {

        try {
            Method method = getIsScreenOnMethod();
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_34_ANDROID_14) {
                return (boolean) method.invoke(manager, displayId);
            }
            return (boolean) method.invoke(manager);
        } catch (ReflectiveOperationException e) {
            Ln.e("Could not invoke method", e);
            return false;
        }
    }

    /**
     * Reset the device's idle timer, as a touch would (needs DEVICE_POWER,
     * which the shell has). Keeps a woken lock screen from timing out.
     */
    public boolean userActivity(int displayId) {
        if (userActivityUnavailable) {
            return false;
        }
        try {
            if (userActivityMethod == null) {
                if (Build.VERSION.SDK_INT >= AndroidVersions.API_33_ANDROID_13) {
                    userActivityMethod = manager.getClass().getMethod("userActivity", int.class, long.class, int.class, int.class);
                } else {
                    userActivityMethod = manager.getClass().getMethod("userActivity", long.class, int.class, int.class);
                }
            }
            long now = android.os.SystemClock.uptimeMillis();
            // event USER_ACTIVITY_EVENT_OTHER (0), no flags
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_33_ANDROID_13) {
                userActivityMethod.invoke(manager, displayId, now, 0, 0);
            } else {
                userActivityMethod.invoke(manager, now, 0, 0);
            }
            return true;
        } catch (ReflectiveOperationException e) {
            Ln.e("Could not invoke userActivity", e);
            userActivityUnavailable = true;
            return false;
        }
    }
}
