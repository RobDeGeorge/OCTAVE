package org.octave.phoneserver.control;

import android.net.LocalSocket;

import java.io.IOException;

public final class ControlChannel {

    private final ControlMessageReader reader;
    private final DeviceMessageWriter writer;
    private final LocalSocket socket;

    public ControlChannel(LocalSocket controlSocket) throws IOException {
        socket = controlSocket;
        reader = new ControlMessageReader(controlSocket.getInputStream());
        writer = new DeviceMessageWriter(controlSocket.getOutputStream());
    }

    public ControlMessage recv() throws IOException {
        return reader.read();
    }

    /**
     * OCTAVE: from now on a read that gets nothing for this long fails, which ends the session like a closed socket.
     * A link that dies without closing (the phone left the Wi-Fi network) otherwise looks alive for many minutes.
     */
    public void setReadTimeout(int ms) throws IOException {
        socket.setSoTimeout(ms);
    }

    public void send(DeviceMessage msg) throws IOException {
        writer.write(msg);
    }
}
