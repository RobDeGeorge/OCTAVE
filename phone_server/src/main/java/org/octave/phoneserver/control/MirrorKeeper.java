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
 * <li>without the panel-dark policy, a press with the panel lit is "screen
 * off" as on any phone: woken with the panel dark; the next press is the
 * locked case above and lights it;</li>
 * <li>keyguard dismissed at any time: in use; keyguard re-engaged while
 * interactive (lock shortcut / lock-after delay): grace, then blank.</li>
 * </ul>
 * While mirrored the idle timer is reset every 1.5 s, in use or not, so the
 * screen never dims or times out (stay_awake only works on the charger).
 * The panel is blanked with SurfaceControl's power mode (the phone stays
 * awake), which CleanUp restores when the session ends. Every state change
 * is reported to the client as a DeviceMessage so it can show what is going
 * on; the client can also change the policy or take the phone back.
 */
public final class MirrorKeeper {

    private static final long POLL_MS = 250;
    // The lock screen overrides the idle timeout with a few seconds (5 s on a
    // Galaxy S-series), and stay_awake does nothing off the charger, so a
    // cradled locked phone dozes on its own every few seconds. Each doze
    // drops the mirror's frames and looks to the keeper like a power press
    // (it relights the panel). Reset the idle timer well inside that window.
    private static final long USER_ACTIVITY_MS = 1500;

    private static MirrorKeeper instance;

    private final CleanUp cleanUp;
    private volatile DeviceMessageSender sender;
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
    private boolean relightOnWake;  // the panel may be on again once interactive
    private boolean forceRelight;   // ... and must be forced on (it was dark)
    private long lastUserActivityMs;

    private final Runnable pollRunnable = this::poll;

    /** One keeper per server process: its state (in use, panel) outlives a client session. */
    public static synchronized MirrorKeeper get(CleanUp cleanUp, DeviceMessageSender sender) {
        if (instance == null) {
            instance = new MirrorKeeper(cleanUp, sender);
        } else {
            instance.sender = sender;
        }
        return instance;
    }

    /** The client session using this sender is gone; keep running for the next one. */
    public static synchronized void detach(DeviceMessageSender sender) {
        if (instance != null && instance.sender == sender) {
            instance.sender = null;
        }
    }

    /** End of the server process. */
    public static synchronized void shutdown() {
        if (instance != null) {
            instance.stop();
            instance = null;
        }
    }

    private MirrorKeeper(CleanUp cleanUp, DeviceMessageSender sender) {
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
                // Dark even without the panel-dark policy: they pressed the
                // button to turn the screen off, so it goes off
                blankOnWake = true;
                setPanel(false);
                wake();
            } else if (!panelDark && !panelIsDark) {
                // The panel is lit (no panel-dark policy): the press means
                // "screen off", as on any phone. The phone has to stay awake
                // for the mirror, so it is woken with its panel dark; the next
                // press lights it again (below)
                Ln.i("Mirror keeper: power pressed with the panel lit; waking it, panel dark");
                graceDeadline = -1;
                lastDozeMs = -1;
                blankOnWake = true;
                relightOnWake = false;
                setPanel(false);
                wake();
            } else {
                long now = android.os.SystemClock.uptimeMillis();
                boolean again = lastDozeMs >= 0 && now - lastDozeMs < graceMs + 2000;
                lastDozeMs = now;
                blankOnWake = false;
                relightOnWake = true;
                forceRelight = panelDark || panelIsDark;
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
                // Do not trust the wake to relight a SurfaceControl-dark panel.
                // Only when we darkened it (policy or a press): forcing the
                // power mode bypasses the brightness controller and wakes the
                // panel dim (Galaxy S22), so a phone we never darkened is left alone.
                relightOnWake = false;
                if (forceRelight) {
                    forceRelight = false;
                    panelIsDark = true;
                    setPanel(true);
                }
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
            // Mirrored: never let it dim or time out, in the user's hand too.
            // Over USB stay_awake does this, but only on the charger: on Wi-Fi
            // the screen dimmed, slept, and was woken again (a brightness dip)
            long now = android.os.SystemClock.uptimeMillis();
            if (now - lastUserActivityMs >= USER_ACTIVITY_MS) {
                lastUserActivityMs = now;
                ServiceManager.getPowerManager().userActivity(0);
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
            if (on) {
                handler.removeCallbacks(nudgeBrightness);
                handler.removeCallbacks(releaseBrightness);
                handler.postDelayed(nudgeBrightness, BRIGHTNESS_NUDGE_DELAY_MS);
            }
        }
    }

    // Forcing the panel's power mode on bypasses the brightness controller:
    // the panel lit very dim while the phone's slider said 70 % (Galaxy S26
    // Ultra, 2026-11-26; S22 before), until the slider was touched. Do what a
    // touch of the slider does: a temporary level a hair off the real one,
    // then clear it, so the controller sends its real level to the panel again.
    private static final long BRIGHTNESS_NUDGE_DELAY_MS = 300;
    private static final long BRIGHTNESS_NUDGE_HOLD_MS = 100;

    private final Runnable nudgeBrightness = this::doNudgeBrightness;
    private final Runnable releaseBrightness = this::doReleaseBrightness;

    private void doNudgeBrightness() {
        if (panelIsDark) {
            return;
        }
        float level = ServiceManager.getDisplayManager().getBrightness(0);
        float nudge = Float.isNaN(level) ? 0.5f : (level > 0.5f ? level - 0.01f : level + 0.01f);
        if (ServiceManager.getDisplayManager().setTemporaryBrightness(0, nudge)) {
            Ln.i("Mirror keeper: brightness re-sent to the panel (level " + level + ")");
            handler.postDelayed(releaseBrightness, BRIGHTNESS_NUDGE_HOLD_MS);
        }
    }

    private void doReleaseBrightness() {
        ServiceManager.getDisplayManager().setTemporaryBrightness(0, Float.NaN);
    }

    private void report() {
        DeviceMessageSender s = sender;
        if (s != null) {
            s.send(DeviceMessage.createOctavePhoneState(asleep, inUse, panelIsDark));
        }
    }

    private Boolean isKeyguardLocked() {
        return ServiceManager.getWindowManager().isKeyguardLocked();
    }
}
