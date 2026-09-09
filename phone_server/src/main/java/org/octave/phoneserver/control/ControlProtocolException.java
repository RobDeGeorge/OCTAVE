package org.octave.phoneserver.control;

import java.io.IOException;

public class ControlProtocolException extends IOException {
    public ControlProtocolException(String message) {
        super(message);
    }
}
