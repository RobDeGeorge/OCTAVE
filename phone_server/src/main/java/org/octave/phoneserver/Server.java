package org.octave.phoneserver;

import org.octave.phoneserver.audio.AudioCapture;
import org.octave.phoneserver.audio.AudioCodec;
import org.octave.phoneserver.audio.AudioDirectCapture;
import org.octave.phoneserver.audio.AudioEncoder;
import org.octave.phoneserver.audio.AudioPlaybackCapture;
import org.octave.phoneserver.audio.AudioRawRecorder;
import org.octave.phoneserver.audio.AudioSource;
import org.octave.phoneserver.control.ControlChannel;
import org.octave.phoneserver.control.Controller;
import org.octave.phoneserver.control.MirrorKeeper;
import org.octave.phoneserver.device.ConfigurationException;
import org.octave.phoneserver.device.DesktopConnection;
import org.octave.phoneserver.device.Device;
import org.octave.phoneserver.device.NewDisplay;
import org.octave.phoneserver.device.Streamer;
import org.octave.phoneserver.opengl.OpenGLRunner;
import org.octave.phoneserver.util.Ln;
import org.octave.phoneserver.util.LogUtils;
import org.octave.phoneserver.video.CameraCapture;
import org.octave.phoneserver.video.NewDisplayCapture;
import org.octave.phoneserver.video.ScreenCapture;
import org.octave.phoneserver.video.SurfaceCapture;
import org.octave.phoneserver.video.SurfaceEncoder;
import org.octave.phoneserver.video.VideoSource;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;

public final class Server {

    public static final String SERVER_PATH;

    static {
        String[] classPaths = System.getProperty("java.class.path").split(File.pathSeparator);
        // By convention, scrcpy is always executed with the absolute path of scrcpy-server.jar as the first item in the classpath
        SERVER_PATH = classPaths[0];
    }

    private static class Completion {
        private final java.util.concurrent.CountDownLatch done = new java.util.concurrent.CountDownLatch(1);
        private int running;
        private boolean fatalError;

        Completion(int running) {
            this.running = running;
        }

        synchronized void addCompleted(boolean fatalError) {
            --running;
            if (fatalError) {
                this.fatalError = true;
            }
            if (running == 0 || this.fatalError) {
                done.countDown();
            }
        }

        void await() throws InterruptedException {
            done.await();
        }
    }

    private Server() {
        // not instantiable
    }

    private static void scrcpy(Options options) throws IOException, ConfigurationException {
        if (Build.VERSION.SDK_INT < AndroidVersions.API_31_ANDROID_12 && options.getVideoSource() == VideoSource.CAMERA) {
            Ln.e("Camera mirroring is not supported before Android 12");
            throw new ConfigurationException("Camera mirroring is not supported");
        }

        if (Build.VERSION.SDK_INT < AndroidVersions.API_29_ANDROID_10) {
            if (options.getNewDisplay() != null) {
                Ln.e("New virtual display is not supported before Android 10");
                throw new ConfigurationException("New virtual display is not supported");
            }
            if (options.getDisplayImePolicy() != -1) {
                Ln.e("Display IME policy is not supported before Android 10");
                throw new ConfigurationException("Display IME policy is not supported");
            }
        }

        CleanUp cleanUp = null;

        if (options.getCleanup()) {
            cleanUp = CleanUp.start(options);
        }

        Workarounds.apply();

        // OCTAVE: with octave_persist_ms the virtual display (and the apps on
        // it) outlives a client session. The server re-listens on the same
        // socket for up to that long after the client drops; the client
        // reconnects to the same scid and the user keeps what they had open.
        // The session loop runs on its own thread so the main looper keeps
        // serving handlers for the whole process lifetime.
        final long persistMs = options.getOctavePersistMs();
        NewDisplayCapture persistentCapture = null;
        if (persistMs > 0 && options.getVideo() && options.getVideoSource() == VideoSource.DISPLAY && options.getNewDisplay() != null) {
            persistentCapture = new NewDisplayCapture(null, options);
            persistentCapture.setPersistent(true);
        }
        final NewDisplayCapture sharedCapture = persistentCapture;
        final CleanUp finalCleanUp = cleanUp;
        final Handler mainHandler = new Handler(Looper.getMainLooper());
        final Throwable[] failure = new Throwable[1];

        Thread sessions = new Thread(() -> {
            try {
                boolean first = true;
                while (true) {
                    DesktopConnection connection;
                    try {
                        connection = DesktopConnection.open(options.getScid(), options.isTunnelForward(), options.getVideo(),
                                options.getAudio(), options.getControl(), options.getSendDummyByte(), first ? 0 : persistMs);
                    } catch (IOException e) {
                        if (first) {
                            throw e;
                        }
                        Ln.i("No client reconnected within " + persistMs + " ms; exiting");
                        break;
                    }
                    if (!first) {
                        Ln.i("Client reconnected; resuming the existing virtual display");
                    }
                    first = false;
                    runSession(connection, options, finalCleanUp, sharedCapture, mainHandler);
                    if (sharedCapture == null) {
                        break;
                    }
                    Ln.i("Client gone; keeping the virtual display for " + persistMs + " ms");
                }
            } catch (Throwable t) {
                failure[0] = t;
            } finally {
                MirrorKeeper.shutdown();
                if (sharedCapture != null) {
                    sharedCapture.destroy();
                }
                if (finalCleanUp != null) {
                    finalCleanUp.interrupt();
                }
                OpenGLRunner.quit(); // quit the OpenGL thread, if any
                try {
                    if (finalCleanUp != null) {
                        finalCleanUp.join();
                    }
                    OpenGLRunner.join();
                } catch (InterruptedException e) {
                    // ignore
                }
                Looper.getMainLooper().quitSafely();
            }
        }, "sessions");
        sessions.start();

        Looper.loop();

        try {
            sessions.join();
        } catch (InterruptedException e) {
            // ignore
        }
        if (failure[0] instanceof IOException) {
            throw (IOException) failure[0];
        }
        if (failure[0] instanceof ConfigurationException) {
            throw (ConfigurationException) failure[0];
        }
        if (failure[0] != null) {
            throw new IOException(failure[0]);
        }
    }

    /** One client session: returns when the client is gone (sockets closed) or a processor failed. */
    private static void runSession(DesktopConnection connection, Options options, CleanUp cleanUp, NewDisplayCapture sharedCapture,
            Handler mainHandler) throws IOException, ConfigurationException {
        boolean control = options.getControl();
        boolean video = options.getVideo();
        boolean audio = options.getAudio();

        List<AsyncProcessor> asyncProcessors = new ArrayList<>();
        try {
            if (options.getSendDeviceMeta()) {
                connection.sendDeviceMeta(Device.getDeviceName());
            }

            Controller controller = null;

            if (control) {
                ControlChannel controlChannel = connection.getControlChannel();
                // Created on the main thread like upstream (clipboard manager)
                java.util.concurrent.FutureTask<Controller> task = new java.util.concurrent.FutureTask<>(
                        () -> new Controller(controlChannel, cleanUp, options));
                mainHandler.post(task);
                try {
                    controller = task.get();
                } catch (InterruptedException | java.util.concurrent.ExecutionException e) {
                    throw new IOException("Could not create controller", e);
                }
                asyncProcessors.add(controller);
            }

            if (audio) {
                AudioCodec audioCodec = options.getAudioCodec();
                AudioSource audioSource = options.getAudioSource();
                AudioCapture audioCapture;
                if (audioSource.isDirect()) {
                    audioCapture = new AudioDirectCapture(audioSource);
                } else {
                    audioCapture = new AudioPlaybackCapture(options.getAudioDup());
                }

                Streamer audioStreamer = new Streamer(connection.getAudioFd(), audioCodec, options.getSendCodecMeta(), options.getSendFrameMeta());
                AsyncProcessor audioRecorder;
                if (audioCodec == AudioCodec.RAW) {
                    audioRecorder = new AudioRawRecorder(audioCapture, audioStreamer);
                } else {
                    audioRecorder = new AudioEncoder(audioCapture, audioStreamer, options);
                }
                asyncProcessors.add(audioRecorder);
            }

            if (video) {
                Streamer videoStreamer = new Streamer(connection.getVideoFd(), options.getVideoCodec(), options.getSendCodecMeta(),
                        options.getSendFrameMeta());
                SurfaceCapture surfaceCapture;
                if (sharedCapture != null) {
                    sharedCapture.setVirtualDisplayListener(controller);
                    surfaceCapture = sharedCapture;
                } else if (options.getVideoSource() == VideoSource.DISPLAY) {
                    NewDisplay newDisplay = options.getNewDisplay();
                    if (newDisplay != null) {
                        surfaceCapture = new NewDisplayCapture(controller, options);
                    } else {
                        assert options.getDisplayId() != Device.DISPLAY_ID_NONE;
                        surfaceCapture = new ScreenCapture(controller, options);
                    }
                } else {
                    surfaceCapture = new CameraCapture(options);
                }
                SurfaceEncoder surfaceEncoder = new SurfaceEncoder(surfaceCapture, videoStreamer, options);
                asyncProcessors.add(surfaceEncoder);

                if (controller != null) {
                    controller.setSurfaceCapture(surfaceCapture);
                }
            }

            Completion completion = new Completion(asyncProcessors.size());
            for (AsyncProcessor asyncProcessor : asyncProcessors) {
                asyncProcessor.start((fatalError) -> {
                    completion.addCompleted(fatalError);
                });
            }

            try {
                completion.await();
            } catch (InterruptedException e) {
                // fall through to teardown
            }
        } finally {
            for (AsyncProcessor asyncProcessor : asyncProcessors) {
                asyncProcessor.stop();
            }

            connection.shutdown();

            try {
                for (AsyncProcessor asyncProcessor : asyncProcessors) {
                    asyncProcessor.join();
                }
            } catch (InterruptedException e) {
                // ignore
            }

            connection.close();
        }
    }

    private static void prepareMainLooper() {
        // Like Looper.prepareMainLooper(), but with quitAllowed set to true
        Looper.prepare();
        synchronized (Looper.class) {
            try {
                @SuppressLint("DiscouragedPrivateApi")
                Field field = Looper.class.getDeclaredField("sMainLooper");
                field.setAccessible(true);
                field.set(null, Looper.myLooper());
            } catch (ReflectiveOperationException e) {
                throw new AssertionError(e);
            }
        }
    }

    public static void main(String... args) {
        int status = 0;
        try {
            internalMain(args);
        } catch (Throwable t) {
            Ln.e(t.getMessage(), t);
            status = 1;
        } finally {
            // By default, the Java process exits when all non-daemon threads are terminated.
            // The Android SDK might start some non-daemon threads internally, preventing the scrcpy server to exit.
            // So force the process to exit explicitly.
            System.exit(status);
        }
    }

    private static void internalMain(String... args) throws Exception {
        Thread.UncaughtExceptionHandler defaultHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            Ln.e("Exception on thread " + t, e);
            if (defaultHandler != null) {
                defaultHandler.uncaughtException(t, e);
            }
        });

        prepareMainLooper();

        Options options = Options.parse(args);

        Ln.disableSystemStreams();
        Ln.initLogLevel(options.getLogLevel());

        Ln.i("Device: [" + Build.MANUFACTURER + "] " + Build.BRAND + " " + Build.MODEL + " (Android " + Build.VERSION.RELEASE + ")");

        if (options.getList()) {
            if (options.getCleanup()) {
                CleanUp.unlinkSelf();
            }

            if (options.getListEncoders()) {
                Ln.i(LogUtils.buildVideoEncoderListMessage());
                Ln.i(LogUtils.buildAudioEncoderListMessage());
            }
            if (options.getListDisplays()) {
                Ln.i(LogUtils.buildDisplayListMessage());
            }
            if (options.getListCameras() || options.getListCameraSizes()) {
                Workarounds.apply();
                Ln.i(LogUtils.buildCameraListMessage(options.getListCameraSizes()));
            }
            if (options.getListApps()) {
                Workarounds.apply();
                Ln.i("Processing Android apps... (this may take some time)");
                Ln.i(LogUtils.buildAppListMessage());
            }
            // Just print the requested data, do not mirror
            return;
        }

        try {
            scrcpy(options);
        } catch (ConfigurationException e) {
            // Do not print stack trace, a user-friendly error-message has already been logged
        }
    }
}
