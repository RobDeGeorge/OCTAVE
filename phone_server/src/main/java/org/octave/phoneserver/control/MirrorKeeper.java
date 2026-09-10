package org.octave.phoneserver.control;

import org.octave.phoneserver.CleanUp;
import org.octave.phoneserver.device.Device;
import org.octave.phoneserver.util.Ln;
import org.octave.phoneserver.wrappers.DisplayManager;
import org.octave.phoneserver.wrappers.ServiceManager;

import android.os.Handler;
import android.os.HandlerThread;
import android.view.KeyEvent;

/**
 * OCTAVE extension: keep the mirrored (virtual) display alive across the
 * phone's power button, and keep the handset's own panel dark while it is
 * mirrored, from inside the phone where the reaction takes milliseconds.
 * <p>
 * A virtual display has no power group of its own on the devices we tested
 * (FLAG_OWN_DISPLAY_GROUP is requested but the display lands in group 0), so
 * when the phone dozes the virtual display dozes with it: content stops and
 * touch is dropped. The only way to keep mirroring is to wake the phone
 * again. What the user meant by the press is decided here:
 * <ul>
 * <li>press while locked (phone in the cradle): wake it and force the panel
 * on (the wake alone does not reliably relight a SurfaceControl-dark panel),
 * leave it lit for a grace window; if the keyguard is dismissed in that
 * window, or the phone turns out to be unlocked already, or the button is
 * pressed again inside the window (someone trying to get in), the user wants
 * their phone ("in use": panel left alone, locks not undone); otherwise the
 * panel is blanked;</li>
 * <li>press while in use: the user is putting the phone down; blank the panel
 * before the wake and again as soon as the phone is interactive;</li>
 * <li>keyguard dismissed at any time: in use; keyguard re-engaged while
 * interactive (lock shortcut / lock-after delay): grace, then blank.</li>
 * </ul>
 * The panel is blanked with SurfaceControl's power mode (the phone stays
 * awake), which CleanUp restores when the session ends. Every state change
 * is reported to the client as a DeviceMessage so it can show what is going
 * on; the client can also change the policy or take the phone back.
 */
public final class MirrorKeeper {

    private static final long POLL_MS = 250;

    private final CleanUp cleanUp;
    private final DeviceMessageSender sender;
    private final HandlerThread thread;
    private final Handler handler;
    private DisplayManager.DisplayListenerHandle displayListener;

    // policy (from the client)
    private boolean enabled;
    private boolean panelDark;
    private long graceMs = 5000;

    // state
    private boolean started;
    private boolean asleep;
    private boolean inUse;
    private boolean prevLocked;
    private boolean lockedKnown;
    private boolean panelIsDark;
    private long graceDeadline = -1;   // uptime ms, -1 = no grace pending
    private boolean blankOnWake;
    private long lastWakeMs;
    private long lastDozeMs = -1;   // uptime ms of the last observed doze
    private boolean relightOnWake;  // force the panel on once interactive again

    private final Runnable pollRunnable = this::poll;

    public MirrorKeeper(CleanUp cleanUp, DeviceMessageSender sender) {
        this.cleanUp = cleanUp;
        this.sender = sender;
        thread = new HandlerThread("mirror-keeper");
        thread.start();
        handler = new Handler(thread.getLooper());
    }

    public void setPolicy(boolean enabled, boolean panelDark, int graceMs) {
        handler.post(() -> applyPolicy(enabled, panelDark, graceMs));
    }

    /** Leave the in-use state: wake if needed, blank per policy. */
    public void takeBack() {
        handler.post(this::doTakeBack);
    }

    public void stop() {
        handler.post(() -> {
            handler.removeCallbacks(pollRunnable);
            if (displayListener != null) {
                ServiceManager.getDisplayManager().unregisterDisplayListener(displayListener);
                displayListener = null;
            }
        });
        thread.quitSafely();
    }

    // ── handler thread ────────────────────────────────────────────────

    private void applyPolicy(boolean newEnabled, boolean newPanelDark, int newGraceMs) {
        graceMs = newGraceMs;
        boolean darkChanged = newPanelDark != panelDark;
        panelDark = newPanelDark;
        if (newEnabled && !started) {
            started = true;
            enabled = true;
            displayListener = ServiceManager.getDisplayManager().registerDisplayListener(displayId -> {
                if (displayId == 0) {
                    handler.post(this::poll);
                }
            }, handler);
            // Initial state: an unlocked phone is in the user's hand, a locked
            // one is in the cradle and gets the panel-off.
            asleep = !Device.isScreenOn(0);
            Boolean locked = isKeyguardLocked();
            lockedKnown = locked != null;
            prevLocked = locked == null || locked;
            if (!asleep && locked != null && !locked) {
                setInUse(true);
            } else if (!asleep) {
                setPanel(!panelDark);
            }
            if (asleep) {
                wake();
            }
            Ln.i("Mirror keeper: started (panel " + (panelDark ? "dark" : "lit") + ", keyguard "
                    + (locked == null ? "unknown" : (locked ? "locked" : "unlocked")) + ")");
            report();
            handler.post(pollRunnable);
            return;
        }
        enabled = newEnabled;
        if (started && darkChanged && !inUse) {
            setPanel(!panelDark);
        }
        report();
    }

    private void doTakeBack() {
        if (!started) {
            return;
        }
        graceDeadline = -1;
        lastDozeMs = -1;
        setInUse(false);
        if (asleep) {
            blankOnWake = panelDark;
            if (blankOnWake) {
                setPanel(false);
            }
            wake();
        } else {
            setPanel(!panelDark);
        }
        report();
    }

    private void poll() {
        if (!started) {
            return;
        }
        handler.removeCallbacks(pollRunnable);
        try {
            evaluate();
        } catch (Throwable t) {
            Ln.e("Mirror keeper error", t);
        }
        handler.postDelayed(pollRunnable, POLL_MS);
    }

    private void evaluate() {
        boolean interactive = Device.isScreenOn(0);
        boolean changed = false;

        if (!interactive && !asleep) {
            asleep = true;
            changed = true;
            if (!enabled) {
                Ln.i("Mirror keeper: phone went to sleep (keeper disabled)");
            } else if (inUse) {
                // Putting the phone down: no grace, dark before the wake
                Ln.i("Mirror keeper: phone put to sleep after use; waking it, panel dark");
                setInUse(false);
                graceDeadline = -1;
                lastDozeMs = -1;
                blankOnWake = panelDark;
                if (blankOnWake) {
                    setPanel(false);
                }
                wake();
            } else {
                long now = android.os.SystemClock.uptimeMillis();
                boolean again = lastDozeMs >= 0 && now - lastDozeMs < graceMs + 2000;
                lastDozeMs = now;
                blankOnWake = false;
                relightOnWake = true;
                if (again) {
                    // Second press inside the window: nothing visible happened
                    // for them the first time, so they are trying to get in.
                    Ln.i("Mirror keeper: power pressed again; user wants the phone");
                    graceDeadline = -1;
                    inUse = true;
                } else {
                    Ln.i("Mirror keeper: phone went to sleep; waking it" + (panelDark ? " (grace " + graceMs + " ms)" : ""));
                }
                wake();
            }
        } else if (interactive && asleep) {
            asleep = false;
            changed = true;
            Ln.i("Mirror keeper: phone is awake again");
            panelIsDark = false;   // the wake resets the panel mode as far as we know
            if (blankOnWake) {
                blankOnWake = false;
                setPanel(false);
            } else if (relightOnWake) {
                // Do not trust the wake to relight a SurfaceControl-dark panel
                relightOnWake = false;
                panelIsDark = true;
                setPanel(true);
                Boolean locked = isKeyguardLocked();
                if (locked != null && !locked && !inUse) {
                    // Woke to an unlocked phone (lock-after delay had not run):
                    // it is lit in their hand, treat it as in use.
                    Ln.i("Mirror keeper: awake and unlocked; phone in use");
                    inUse = true;
                }
                if (!inUse && panelDark && enabled) {
                    graceDeadline = android.os.SystemClock.uptimeMillis() + graceMs;
                }
            }
        }

        if (!asleep && enabled) {
            Boolean lockedObj = isKeyguardLocked();
            if (lockedObj != null) {
                boolean locked = lockedObj;
                if (lockedKnown && locked != prevLocked) {
                    Ln.i("Mirror keeper: keyguard " + (locked ? "engaged" : "dismissed"));
                }
                if (lockedKnown) {
                    if (prevLocked && !locked && !inUse) {
                        Ln.i("Mirror keeper: phone in use");
                        graceDeadline = -1;
                        setInUse(true);
                        changed = true;
                    } else if (!prevLocked && locked && inUse) {
                        Ln.i("Mirror keeper: keyguard re-engaged; grace " + graceMs + " ms");
                        setInUse(false);
                        graceDeadline = android.os.SystemClock.uptimeMillis() + (panelDark ? graceMs : 0);
                        changed = true;
                    }
                }
                prevLocked = locked;
                lockedKnown = true;
            }
            if (graceDeadline >= 0 && android.os.SystemClock.uptimeMillis() >= graceDeadline) {
                graceDeadline = -1;
                if (panelDark && !inUse) {
                    setPanel(false);
                }
            }
        }

        if (changed) {
            report();
        }
    }

    private void wake() {
        long now = android.os.SystemClock.uptimeMillis();
        if (now - lastWakeMs < 500) {
            return;   // one wake per press; the poll re-evaluates anyway
        }
        lastWakeMs = now;
        // KEYCODE_WAKEUP wakes and never toggles the device off
        Device.pressReleaseKeycode(KeyEvent.KEYCODE_WAKEUP, 0, Device.INJECT_MODE_ASYNC);
    }

    private void setInUse(boolean value) {
        if (value == inUse) {
            return;
        }
        inUse = value;
        if (inUse) {
            setPanel(true);   // they are holding it: never leave it dark
        }
    }

    private void setPanel(boolean on) {
        if (panelIsDark == !on) {
            return;
        }
        if (Device.setDisplayPower(0, on)) {
            panelIsDark = !on;
            Ln.i("Device display turned " + (on ? "on" : "off"));
            if (cleanUp != null) {
                cleanUp.setRestoreDisplayPower(!on);
            }
        }
    }

    private void report() {
        sender.send(DeviceMessage.createOctavePhoneState(asleep, inUse, panelIsDark));
    }

    private Boolean isKeyguardLocked() {
        return ServiceManager.getWindowManager().isKeyguardLocked();
    }
}
