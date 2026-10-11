package org.octave.phoneserver.control;

import org.octave.phoneserver.util.Ln;

import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

public final class DeviceMessageSender {

    private final ControlChannel controlChannel;

    private Thread thread;
    private final BlockingQueue<DeviceMessage> queue = new ArrayBlockingQueue<>(16);
    // OCTAVE: bulk messages (queue art) wait for room instead of being
    // dropped, and only go out when no regular message is waiting, so a
    // burst of art never delays or crowds out state updates
    private final BlockingQueue<DeviceMessage> bulkQueue = new ArrayBlockingQueue<>(8);
    private final Semaphore pending = new Semaphore(0);   // one permit per queued message, either lane
    private volatile boolean stopped;

    public DeviceMessageSender(ControlChannel controlChannel) {
        this.controlChannel = controlChannel;
    }

    public void send(DeviceMessage msg) {
        if (queue.offer(msg)) {
            pending.release();
        } else {
            Ln.w("Device message dropped: " + msg.getType());
        }
    }

    /** Blocks the caller until there is room (or the sender stops); call from a worker thread. */
    public void sendBulk(DeviceMessage msg) {
        try {
            while (!stopped) {
                if (bulkQueue.offer(msg, 200, TimeUnit.MILLISECONDS)) {
                    pending.release();
                    return;
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private void loop() throws IOException, InterruptedException {
        while (!Thread.currentThread().isInterrupted()) {
            pending.acquire();
            DeviceMessage msg = queue.poll();
            if (msg == null) {
                msg = bulkQueue.poll();
            }
            if (msg != null) {
                controlChannel.send(msg);
            }
        }
    }

    public void start() {
        thread = new Thread(() -> {
            try {
                loop();
            } catch (IOException | InterruptedException e) {
                // this is expected on close
            } finally {
                Ln.d("Device message sender stopped");
            }
        }, "control-send");
        thread.start();
    }

    public void stop() {
        stopped = true;
        if (thread != null) {
            thread.interrupt();
        }
    }

    public void join() throws InterruptedException {
        if (thread != null) {
            thread.join();
        }
    }
}
