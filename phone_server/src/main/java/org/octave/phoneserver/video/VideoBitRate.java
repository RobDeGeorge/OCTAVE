package org.octave.phoneserver.video;

import org.octave.phoneserver.util.Ln;

import android.media.MediaCodec;
import android.os.Bundle;

/**
 * OCTAVE extension: the video bit rate the client currently wants
 * (TYPE_OCTAVE_SET_BIT_RATE, OCTAVE's adaptive quality). Applied live to the
 * running encoder through MediaCodec.setParameters, and used for the format of
 * every encoder created afterwards (capture reset, display resize).
 * Process-wide: the server streams one video at a time.
 */
public final class VideoBitRate {

    private static int bitRate;            // 0 = the session's video_bit_rate option
    private static MediaCodec runningCodec;

    private VideoBitRate() {
        // not instantiable
    }

    /** The rate for a new encoder: the last requested one, else the option. */
    public static synchronized int current(int optionBitRate) {
        return bitRate > 0 ? bitRate : optionBitRate;
    }

    public static synchronized void setRunningCodec(MediaCodec codec) {
        runningCodec = codec;
    }

    public static synchronized void set(int value) {
        if (value <= 0 || value == bitRate) {
            return;
        }
        bitRate = value;
        if (runningCodec != null) {
            Bundle params = new Bundle();
            params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, value);
            try {
                runningCodec.setParameters(params);
            } catch (IllegalStateException e) {
                Ln.w("Could not change the video bit rate: " + e.getMessage());
            }
        }
    }
}
