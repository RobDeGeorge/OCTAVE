/*
 * OCTAVE Android media bridge.
 *
 * Exposes synchronous Java methods to the C++ backend (via QJniObject) for:
 *   - Local file metadata + embedded album art (Android MediaMetadataRetriever)
 *   - The app's own Music dir (scoped-storage-safe library root)
 */
package org.octave.app;

import android.content.Context;
import android.media.MediaMetadataRetriever;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public class OctaveMediaBridge {
    private static final String TAG = "OctaveMediaBridge";

    // ═══════════════════════════════════════════════════════════════════
    // Local file metadata via Android MediaMetadataRetriever
    // ═══════════════════════════════════════════════════════════════════

    public static String readMetadata(String filePath) {
        MediaMetadataRetriever mmr = new MediaMetadataRetriever();
        try {
            mmr.setDataSource(filePath);
            String title  = safe(mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE));
            String artist = safe(mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST));
            String album  = safe(mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM));
            String durMs  = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            long durSecs  = 0;
            if (durMs != null && !durMs.isEmpty()) {
                try { durSecs = Long.parseLong(durMs) / 1000L; } catch (NumberFormatException ignored) {}
            }
            return "title\n" + title + "\nartist\n" + artist +
                   "\nalbum\n" + album + "\nduration\n" + durSecs;
        } catch (Throwable t) {
            Log.w(TAG, "readMetadata failed for " + filePath, t);
            return "";
        } finally {
            try { mmr.release(); } catch (IOException ignored) {}
        }
    }

    /**
     * Returns an absolute writable Music directory under the app's external
     * files dir. Scoped-storage-safe on Android 10+; survives
     * until the app is uninstalled; no permissions required. Creates it if
     * missing. Result path is readable by file explorers and MediaStore.
     */
    public static String getAppMusicDir(Context ctx) {
        try {
            File dir = ctx.getExternalFilesDir("Music");
            if (dir == null) dir = new File(ctx.getFilesDir(), "Music");
            if (!dir.exists()) dir.mkdirs();
            return dir.getAbsolutePath();
        } catch (Throwable t) {
            Log.w(TAG, "getAppMusicDir failed", t);
            return "";
        }
    }

    public static boolean extractAlbumArt(String filePath, String outPath) {
        MediaMetadataRetriever mmr = new MediaMetadataRetriever();
        try {
            mmr.setDataSource(filePath);
            byte[] bytes = mmr.getEmbeddedPicture();
            if (bytes == null || bytes.length == 0) return false;
            File parent = new File(outPath).getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            try (FileOutputStream fos = new FileOutputStream(outPath)) {
                fos.write(bytes);
            }
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "extractAlbumArt failed for " + filePath, t);
            return false;
        } finally {
            try { mmr.release(); } catch (IOException ignored) {}
        }
    }

    private static String safe(String s) { return s == null ? "" : s; }
}
