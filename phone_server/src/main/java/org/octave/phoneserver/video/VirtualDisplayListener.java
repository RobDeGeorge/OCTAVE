package org.octave.phoneserver.video;

import org.octave.phoneserver.control.PositionMapper;

public interface VirtualDisplayListener {
    void onNewVirtualDisplay(int displayId, PositionMapper positionMapper);
}
