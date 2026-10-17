/*
 * OCTAVE Android media bridge.
 *
 * Exposes synchronous Java methods to the C++ backend (via QJniObject) for:
 *   - Local file metadata + embedded album art (Android MediaMetadataRetriever)
 *   - Downloads dir / All-Files-Access helpers and HTTPS image caching
 *
 * Always compiled. yt-dlp / ytmusicapi live in OctaveDownloadBridge
 * (src-downloads/), which app-store builds leave out.
 */
package org.octave.app;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;

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
     * Returns an absolute writable directory for downloaded music under the
     * app's external files dir. Scoped-storage-safe on Android 10+; survives
     * until the app is uninstalled; no permissions required. Creates it if
     * missing. Result path is readable by file explorers and MediaStore.
     */
    public static String getDownloadsDir(Context ctx) {
        try {
            File dir = ctx.getExternalFilesDir("Music");
            if (dir == null) dir = new File(ctx.getFilesDir(), "Music");
            if (!dir.exists()) dir.mkdirs();
            return dir.getAbsolutePath();
        } catch (Throwable t) {
            Log.w(TAG, "getDownloadsDir failed", t);
            return "";
        }
    }

    /**
     * Returns true when the app holds Android 11+ "All files access" (the
     * MANAGE_EXTERNAL_STORAGE special permission). When true, POSIX writes
     * into /storage/emulated/0/Music/... and similar shared paths work.
     * Otherwise the caller must fall back to getDownloadsDir().
     */
    public static boolean hasAllFilesAccess() {
        try {
            return Environment.isExternalStorageManager();
        } catch (Throwable t) {
            Log.w(TAG, "hasAllFilesAccess check failed", t);
            return false;
        }
    }

    /**
     * Opens the system Settings page that lets the user toggle "All files
     * access" for this app. MANAGE_EXTERNAL_STORAGE can't be requested via a
     * normal runtime permission dialog — the user has to flip it in Settings.
     * Returns true if the intent was launched.
     */
    public static boolean requestAllFilesAccess(Activity activity) {
        if (activity == null) return false;
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + activity.getPackageName()));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity.startActivity(intent);
            return true;
        } catch (Throwable t) {
            // Fallback to generic settings if the targeted intent fails on
            // non-standard OEMs (OxygenOS, etc.).
            Log.w(TAG, "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION failed, trying generic", t);
            try {
                Intent generic = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                generic.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                activity.startActivity(generic);
                return true;
            } catch (Throwable t2) {
                Log.w(TAG, "requestAllFilesAccess fallback failed", t2);
                return false;
            }
        }
    }

    /**
     * Download a URL (typically an HTTPS thumbnail) to a local file via
     * Android's HttpURLConnection — uses Android's own SSL stack, which works
     * even when Qt's OpenSSL backend is missing. Returns the absolute local
     * file path on success, "" on failure. Caches by URL hash so repeated
     * calls with the same URL are free.
     */
    public static String cacheHttpImage(Context ctx, String url) {
        if (url == null || url.isEmpty() || !url.startsWith("http")) return "";
        try {
            // Hash-based cache key so repeated calls for the same URL hit cache
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] hash = md.digest(url.getBytes("UTF-8"));
            StringBuilder hex = new StringBuilder();
            for (int i = 0; i < 8; i++) hex.append(String.format("%02x", hash[i]));
            // Always save as .jpg — we re-encode below, so extension is authoritative.
            File cacheDir = new File(ctx.getCacheDir(), "octave-thumbs");
            if (!cacheDir.exists()) cacheDir.mkdirs();
            File out = new File(cacheDir, hex.toString() + ".jpg");
            if (out.exists() && out.length() > 0) return out.getAbsolutePath();

            HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(10000);
            conn.setRequestProperty("User-Agent",
                "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36");
            int rc = conn.getResponseCode();
            if (rc != 200) {
                Log.w(TAG, "cacheHttpImage: HTTP " + rc + " for " + url);
                return "";
            }

            // YouTube returns thumbnails as WebP. Qt for Android's image plugins
            // don't reliably decode WebP, so we read the bytes, decode via
            // BitmapFactory (handles WebP/JPEG/PNG), then re-encode as JPEG.
            // This makes the cache Qt-Image-compatible regardless of source format.
            byte[] bytes;
            try (InputStream in = conn.getInputStream();
                 java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream()) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
                bytes = bos.toByteArray();
            }

            Bitmap bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
            if (bmp == null) {
                Log.w(TAG, "BitmapFactory could not decode image from " + url);
                return "";
            }
            try (FileOutputStream fos = new FileOutputStream(out)) {
                bmp.compress(Bitmap.CompressFormat.JPEG, 85, fos);
            }
            bmp.recycle();
            return out.getAbsolutePath();
        } catch (Throwable t) {
            Log.w(TAG, "cacheHttpImage failed for " + url, t);
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
