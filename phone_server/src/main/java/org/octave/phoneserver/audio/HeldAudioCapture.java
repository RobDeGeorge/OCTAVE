package org.octave.phoneserver.audio;

import org.octave.phoneserver.util.Ln;

import android.media.MediaCodec;
import android.os.SystemClock;

import java.nio.ByteBuffer;

/**
 * OCTAVE: an audio capture that outlives a dropped client for a while, so
 * the phone's music keeps playing through a cable pull.
 *
 * <p>Releasing a capture hands the phone's media back to its own speaker,
 * so the wrapped capture pauses the media when it is released. A link drop
 * (USB cable pulled, Wi-Fi gone) used to do exactly that: the music stopped
 * and nothing started it again once OCTAVE was back over Wi-Fi. This keeps
 * recording between sessions instead (throwing the sound away, which also
 * keeps the phone's speaker silent) for up to {@link #HOLD_MS}; the next
 * session reads on from the same capture. Only when nobody comes back in
 * time, the client asks to end ({@link #endRequested()}, TYPE_OCTAVE_END),
 * or the server exits is the capture released, and the media paused.
 */
public final class HeldAudioCapture implements AudioCapture {

    /** How long the music plays on with no client: the client's reconnect grace (PhoneMirrorManager::kReconnectGraceMs). */
    public static final long HOLD_MS = 10_000;

    private static volatile HeldAudioCapture current;

    private final AudioCapture inner;
    private final Object lock = new Object();
    private boolean recording;      // inner started and not released
    private boolean inSession;      // a session reads from it right now
    private boolean draining;       // held between sessions
    private boolean endRequested;   // the client is going away on purpose
    private Thread drainThread;

    public HeldAudioCapture(AudioCapture inner) {
        this.inner = inner;
        current = this;
    }

    /** The client is stopping on purpose: release (and pause) now, or as soon as the session ends. */
    public static void endRequested() {
        HeldAudioCapture capture = current;
        if (capture != null) {
            capture.requestRelease("the client is stopping");
        }
    }

    @Override
    public void checkCompatibility() throws AudioCaptureException {
        inner.checkCompatibility();
    }

    @Override
    public void start() throws AudioCaptureException {
        stopDrain();
        synchronized (lock) {
            endRequested = false;
            if (!recording) {
                inner.start();
                recording = true;
            } else {
                Ln.i("Audio: resuming the held capture; the music played on");
            }
            inSession = true;
        }
    }

    @Override
    public void stop() {
        synchronized (lock) {
            inSession = false;
            if (!recording) {
                return;
            }
            if (endRequested) {
                releaseLocked();
                return;
            }
            Ln.i("Audio: client gone; keeping the capture (and the music) for " + HOLD_MS + " ms");
            draining = true;
            final long deadline = SystemClock.uptimeMillis() + HOLD_MS;
            drainThread = new Thread(() -> drain(deadline), "audio-hold");
            drainThread.start();
        }
    }

    @Override
    public int read(ByteBuffer outDirectBuffer, MediaCodec.BufferInfo outBufferInfo) {
        return inner.read(outDirectBuffer, outBufferInfo);
    }

    /** Release now (the server is exiting) or once the running session ends. */
    public void requestRelease(String why) {
        stopDrain();
        synchronized (lock) {
            endRequested = true;
            if (recording && !inSession) {
                Ln.i("Audio: releasing the held capture (" + why + ")");
                releaseLocked();
            }
        }
    }

    private void drain(long deadline) {
        // Keep reading so the capture (and so the player) never stalls on a full buffer
        ByteBuffer buffer = ByteBuffer.allocateDirect(AudioConfig.MAX_READ_SIZE);
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        while (true) {
            synchronized (lock) {
                if (!draining) {
                    return;
                }
                if (SystemClock.uptimeMillis() >= deadline) {
                    Ln.i("Audio: no client came back within " + HOLD_MS + " ms; releasing the capture");
                    draining = false;
                    releaseLocked();
                    return;
                }
            }
            buffer.position(0);
            if (inner.read(buffer, info) < 0) {
                synchronized (lock) {
                    if (draining) {
                        Ln.w("Audio: the held capture failed; releasing it");
                        draining = false;
                        releaseLocked();
                    }
                }
                return;
            }
        }
    }

    private void stopDrain() {
        Thread thread;
        synchronized (lock) {
            draining = false;
            thread = drainThread;
            drainThread = null;
        }
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join();   // a read returns within one chunk
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void releaseLocked() {
        recording = false;
        inner.stop();   // pauses the phone's media first (see the wrapped capture)
    }
}
