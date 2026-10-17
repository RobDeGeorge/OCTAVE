/*
 * OCTAVE Android download bridge.
 *
 * Exposes synchronous Java methods to the C++ DownloadManager (via QJniObject):
 *   - yt-dlp search + download (junkfood02/youtubedl-android 0.18.1)
 *   - ffmpeg binary path resolution (library auto-installs it; we grab the
 *     path via reflection and C++ calls it via QProcess directly)
 *   - ytmusicapi search + cover embedding on the AAR's bundled Python
 *
 * Lives in src-downloads/, which android/build.gradle compiles only when
 * OCTAVE_ENABLE_DOWNLOADS is ON; app-store builds leave this class and the
 * youtubedl-android AAR out of the APK entirely. Everything that is not
 * download-specific belongs in OctaveMediaBridge (src/).
 */
package org.octave.app;

import android.content.Context;
import android.util.Log;
import com.yausername.youtubedl_android.YoutubeDL;
import com.yausername.youtubedl_android.YoutubeDLException;
import com.yausername.youtubedl_android.YoutubeDLRequest;
import com.yausername.youtubedl_android.YoutubeDLResponse;
import com.yausername.ffmpeg.FFmpeg;
import kotlin.Unit;
import kotlin.jvm.functions.Function3;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class OctaveDownloadBridge {
    private static final String TAG = "OctaveDownloadBridge";

    private static volatile boolean ytInitialized = false;
    private static volatile boolean ffInitialized = false;
    private static volatile boolean ytmusicapiReady = false;
    private static volatile boolean ytUpdateAttempted = false;
    private static volatile String initError = null;

    // Single-slot progress state
    private static volatile float currentProgress = 0.0f;
    private static volatile long currentEta = 0;
    private static volatile String currentLine = "";
    private static volatile int lastExitCode = 0;

    // Kotlin Function3 adapter for the yt-dlp progress callback.
    private static class ProgressFn implements Function3<Float, Long, String, Unit> {
        @Override
        public Unit invoke(Float progress, Long etaInSeconds, String line) {
            currentProgress = progress == null ? 0.0f : progress;
            currentEta = etaInSeconds == null ? 0 : etaInSeconds;
            currentLine = line == null ? "" : line;
            return Unit.INSTANCE;
        }
    }

    public static synchronized boolean init(Context ctx) {
        if (ytInitialized && ffInitialized) return true;
        try {
            if (!ytInitialized) {
                YoutubeDL.getInstance().init(ctx);
                ytInitialized = true;
                Log.i(TAG, "yt-dlp initialized");
            }
            if (!ffInitialized) {
                FFmpeg.getInstance().init(ctx);
                ffInitialized = true;
                Log.i(TAG, "ffmpeg initialized");
            }
            // Opportunistic — non-fatal if it fails, search will fall back to yt-dlp.
            ensureYtmusicapi(ctx);
            // Best-effort yt-dlp self-update on a background thread. The
            // youtubedl-android AAR bundles a snapshot of yt-dlp from when
            // the AAR was published; YouTube's bot detection rotates faster
            // than that, so without this update most downloads fail with
            // "Sign in to confirm you're not a bot." The NIGHTLY channel has
            // the most current YouTube extractor patches.
            if (!ytUpdateAttempted) {
                ytUpdateAttempted = true;
                final Context appCtx = ctx.getApplicationContext();
                new Thread(() -> {
                    // youtubedl-android 0.18.1's built-in updater (updateYoutubeDL)
                    // is permanently broken on Android: it depends on
                    // commons-io's Java7Support whose static initializer NPEs
                    // on Android because getClassLoader() returns null in this
                    // context. We bypass it entirely and download the yt-dlp
                    // zipapp directly from yt-dlp/yt-dlp's GitHub release page,
                    // overwriting the AAR-bundled binary in place. The AAR's
                    // execute() reads from this same path on every call, so
                    // the next download attempt picks up the new version.
                    boolean updated = directDownloadYtDlp(appCtx);
                    Log.i(TAG, "yt-dlp self-update finished (updated=" + updated + ")");
                }, "ytdlp-updater").start();
            }
            initError = null;
            return true;
        } catch (YoutubeDLException e) {
            initError = e.getMessage();
            Log.e(TAG, "init failed", e);
            return false;
        } catch (Throwable t) {
            initError = t.getMessage();
            Log.e(TAG, "init failed (unexpected)", t);
            return false;
        }
    }

    public static String lastInitError() { return initError == null ? "" : initError; }

    /**
     * Direct yt-dlp downloader -- bypasses youtubedl-android's broken updater.
     *
     * The AAR stores its yt-dlp zipapp at
     *     {noBackupFilesDir}/youtubedl-android/yt-dlp/yt-dlp
     * and the library's execute() invokes that same path on every call. So if
     * we overwrite that file with a fresher zipapp from the yt-dlp project's
     * GitHub release page, all future execute() calls use the new version.
     *
     * Best-effort, non-fatal: any failure (no network, GitHub down, etc.)
     * leaves the AAR-bundled fallback in place.
     */
    private static boolean directDownloadYtDlp(Context ctx) {
        // yt-dlp/yt-dlp ships a self-contained zipapp called "yt-dlp" on every
        // release; /releases/latest/download/ always 302's to the current
        // release's binary, so we never need to resolve a version tag.
        final String url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp";
        File targetDir = new File(ctx.getNoBackupFilesDir(), "youtubedl-android/yt-dlp");
        File target = new File(targetDir, "yt-dlp");
        if (!targetDir.exists() && !targetDir.mkdirs()) {
            Log.w(TAG, "yt-dlp updater: cannot create " + targetDir);
            return false;
        }
        File tmp = new File(targetDir, "yt-dlp.new");
        HttpURLConnection conn = null;
        try {
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setInstanceFollowRedirects(true);
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(60000);
            int code = conn.getResponseCode();
            if (code / 100 != 2) {
                Log.w(TAG, "yt-dlp updater: HTTP " + code + " from " + url);
                return false;
            }
            try (InputStream in = conn.getInputStream();
                 FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                long total = 0;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    total += n;
                }
                Log.i(TAG, "yt-dlp updater: downloaded " + total + " bytes");
                if (total < 100_000) {
                    Log.w(TAG, "yt-dlp updater: download too small, aborting");
                    return false;
                }
            }
            // Sanity check: zipapps start with "#!" shebang.
            try (java.io.FileInputStream fis = new java.io.FileInputStream(tmp)) {
                int b1 = fis.read();
                int b2 = fis.read();
                if (b1 != '#' || b2 != '!') {
                    Log.w(TAG, "yt-dlp updater: download is not a zipapp (no shebang)");
                    return false;
                }
            }
            // Atomic-ish replace.
            if (target.exists() && !target.delete()) {
                Log.w(TAG, "yt-dlp updater: cannot delete existing " + target);
                return false;
            }
            if (!tmp.renameTo(target)) {
                Log.w(TAG, "yt-dlp updater: rename failed");
                return false;
            }
            target.setExecutable(true, false);
            Log.i(TAG, "yt-dlp updater: replaced " + target + " (" + target.length() + " bytes)");
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "yt-dlp updater threw " + t.getClass().getSimpleName()
                + ": " + t.getMessage(), t);
            return false;
        } finally {
            if (conn != null) conn.disconnect();
            if (tmp.exists()) tmp.delete();
        }
    }

    /**
     * Run yt-dlp with the given args. Blocks until done. Returns combined
     * stdout+stderr. Exit code via lastExitCode(). Progress via poll*().
     */
    public static String runYtDlp(String[] args) {
        if (!ytInitialized) {
            lastExitCode = -1;
            return "[OCTAVE] yt-dlp not initialized";
        }
        currentProgress = 0.0f;
        currentEta = 0;
        currentLine = "";

        // Split flags from the positional URL/search target.
        //
        // Rule: any arg that looks like a URL/search-target (see isUrl) is
        // positional; otherwise it's a flag, and if a flag is immediately
        // followed by a non-flag, non-URL value that's paired with the flag
        // (e.g. "-f FORMAT", "--extractor-args KEY:VALUE", "-o PATH").
        //
        // This is more robust than a hardcoded list of value-taking flags —
        // yt-dlp has hundreds of them.
        java.util.List<String> urls = new java.util.ArrayList<>();
        java.util.List<String[]> flagPairs = new java.util.ArrayList<>();  // [flag, value-or-null]
        for (int i = 0; i < args.length; i++) {
            String a = args[i];
            if (isUrl(a)) {
                urls.add(a);
            } else if (a.startsWith("-")) {
                String val = null;
                if (i + 1 < args.length && !args[i + 1].startsWith("-") && !isUrl(args[i + 1])) {
                    val = args[i + 1];
                    i++;
                }
                flagPairs.add(new String[]{a, val});
            } else {
                // Rare: bare non-URL positional. Treat as URL anyway so we don't drop it.
                urls.add(a);
            }
        }

        YoutubeDLRequest request = new YoutubeDLRequest(urls.isEmpty() ? "" : urls.get(0));
        // Any additional URLs get appended
        for (int i = 1; i < urls.size(); i++) request.addOption(urls.get(i));
        for (String[] pair : flagPairs) {
            if (pair[1] == null) request.addOption(pair[0]);
            else request.addOption(pair[0], pair[1]);
        }

        try {
            String pid = "octave-" + System.nanoTime();
            YoutubeDLResponse response = YoutubeDL.getInstance().execute(request, pid, new ProgressFn());
            lastExitCode = response.getExitCode();
            return safeOut(response.getOut()) + safeOut(response.getErr());
        } catch (Throwable t) {
            lastExitCode = -2;
            Log.e(TAG, "yt-dlp execute failed", t);
            return "[OCTAVE] yt-dlp error: " + t.getMessage();
        }
    }

    // Detects URL-like positional arguments so the splitter doesn't mistake
    // them for flag values. Covers http(s) URLs and yt-dlp's ytsearch scheme.
    private static boolean isUrl(String s) {
        if (s == null || s.isEmpty()) return false;
        return s.startsWith("http://") || s.startsWith("https://")
            || s.startsWith("ftp://") || s.startsWith("ytsearch")
            || s.startsWith("ytsearchdate") || s.startsWith("ytsearchmusic");
    }

    /**
     * Returns the absolute path to the library-installed ffmpeg binary, or ""
     * if unavailable. Uses reflection to read FFmpeg.INSTANCE.binDir since
     * that field is private in the AAR's compiled classes.
     */
    public static String getFfmpegBinaryPath() {
        if (!ffInitialized) return "";
        try {
            FFmpeg ff = FFmpeg.getInstance();
            Field binDirField = FFmpeg.class.getDeclaredField("binDir");
            binDirField.setAccessible(true);
            File binDir = (File) binDirField.get(ff);
            if (binDir == null) return "";
            File bin = new File(binDir, "ffmpeg");
            if (!bin.exists()) return "";
            return bin.getAbsolutePath();
        } catch (Throwable t) {
            Log.w(TAG, "could not resolve ffmpeg binary path", t);
            return "";
        }
    }

    public static int lastExitCode() { return lastExitCode; }
    public static float pollProgress() { return currentProgress; }
    public static long pollEta() { return currentEta; }
    public static String pollLine() { return currentLine; }

    // ═══════════════════════════════════════════════════════════════════
    // ytmusicapi — curated YouTube Music search via the bundled Python
    //
    // youtubedl-android 0.18.1 bundles a Python 3.12 runtime but strips pip
    // and ensurepip, so we can't install packages at runtime. Instead we ship
    // ytmusicapi + its pure-Python dep chain (requests, urllib3, idna,
    // charset_normalizer, certifi) as assets/python-packages/ytmusic-bundle.zip
    // and extract them into filesDir/pylibs/ on first launch, then point
    // PYTHONPATH at that dir when invoking ytmusic_search.py.
    //
    // The Python interpreter is the AAR-shipped libpython.so, which lives in
    // the app's native-library dir (Android extracts any lib/<abi>/*.so entry
    // there with exec permission, which is how the AAR launches it).
    // ═══════════════════════════════════════════════════════════════════

    private static final String PYBUNDLE_ASSET  = "python-packages/ytmusic-bundle.zip";
    private static final String PYBUNDLE_MARKER = ".pybundle-v1";
    private static final String SEARCH_SCRIPT_ASSET = "scripts/ytmusic_search.py";
    private static final String COVER_SCRIPT_ASSET  = "scripts/embed_cover.py";

    /**
     * Resolves the libpython.so interpreter binary inside the app's native
     * library dir. Returns null if not yet extracted (ie. yt-dlp init hasn't
     * run) or the AAR layout changed.
     */
    public static String getPythonBinaryPath(Context ctx) {
        File bin = new File(ctx.getApplicationInfo().nativeLibraryDir, "libpython.so");
        return bin.exists() ? bin.getAbsolutePath() : null;
    }

    /**
     * Root of the AAR-extracted Python package tree (PYTHONHOME).
     * Contains lib/python3.12/, etc/tls/cert.pem, and the .so dependencies.
     */
    private static File pythonUsrRoot(Context ctx) {
        return new File(ctx.getNoBackupFilesDir(),
            "youtubedl-android/packages/python/usr");
    }

    /**
     * Root of the AAR-extracted ffmpeg package tree — its lib/ dir contains
     * native shared objects the Python .so's need on LD_LIBRARY_PATH.
     */
    private static File ffmpegUsrRoot(Context ctx) {
        return new File(ctx.getNoBackupFilesDir(),
            "youtubedl-android/packages/ffmpeg/usr");
    }

    /**
     * Extracts the bundled ytmusicapi site-packages zip from assets into
     * filesDir/pylibs/ on first launch. Idempotent via a marker file so
     * subsequent launches are ~free. Blocking — must run off the UI thread.
     */
    private static synchronized void ensureYtmusicapi(Context ctx) {
        if (ytmusicapiReady) return;
        File pylibs = new File(ctx.getFilesDir(), "pylibs");
        File marker = new File(pylibs, PYBUNDLE_MARKER);
        if (marker.exists() && new File(pylibs, "ytmusicapi").isDirectory()) {
            ytmusicapiReady = true;
            return;
        }

        if (pylibs.exists()) deleteRecursive(pylibs);
        if (!pylibs.mkdirs()) {
            Log.w(TAG, "could not create pylibs dir at " + pylibs);
            return;
        }

        try (InputStream in = ctx.getAssets().open(PYBUNDLE_ASSET);
             ZipInputStream zis = new ZipInputStream(in)) {
            ZipEntry entry;
            byte[] buf = new byte[8192];
            while ((entry = zis.getNextEntry()) != null) {
                File out = new File(pylibs, entry.getName());
                // Defend against zip-slip.
                if (!out.getCanonicalPath().startsWith(pylibs.getCanonicalPath() + File.separator)) {
                    Log.w(TAG, "refusing suspicious zip entry: " + entry.getName());
                    continue;
                }
                if (entry.isDirectory()) {
                    out.mkdirs();
                } else {
                    File parent = out.getParentFile();
                    if (parent != null && !parent.exists()) parent.mkdirs();
                    try (FileOutputStream fos = new FileOutputStream(out)) {
                        int n;
                        while ((n = zis.read(buf)) > 0) fos.write(buf, 0, n);
                    }
                }
                zis.closeEntry();
            }
            marker.createNewFile();
            ytmusicapiReady = true;
            Log.i(TAG, "ytmusicapi bundle extracted to " + pylibs);
        } catch (Throwable t) {
            Log.w(TAG, "ytmusicapi bundle extract failed", t);
        }
    }

    /**
     * Copies a bundled Python helper from assets/ into a writable path the
     * interpreter can execute. Returns the absolute path, or null on failure.
     * Re-uses the extracted file on subsequent calls — pass a fresh outName
     * when the asset changes to force a rewrite.
     */
    private static String extractScriptAsset(Context ctx, String assetName, String outName) {
        File out = new File(ctx.getFilesDir(), outName);
        if (out.exists() && out.length() > 0) return out.getAbsolutePath();
        try (InputStream in = ctx.getAssets().open(assetName);
             FileOutputStream fos = new FileOutputStream(out)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
        } catch (Throwable t) {
            Log.w(TAG, "script extract failed for " + assetName, t);
            return null;
        }
        return out.getAbsolutePath();
    }

    public static String extractSearchScript(Context ctx) {
        return extractScriptAsset(ctx, SEARCH_SCRIPT_ASSET, "ytmusic_search.py");
    }

    /**
     * Populates LD_LIBRARY_PATH / PYTHONHOME / PYTHONPATH / SSL_CERT_FILE /
     * TMPDIR so libpython.so can link and import from the AAR-extracted
     * Python tree + our pylibs bundle.
     */
    private static void applyPythonEnv(Context ctx, ProcessBuilder pb) {
        File pyUsr = pythonUsrRoot(ctx);
        File ffUsr = ffmpegUsrRoot(ctx);
        File pylibs = new File(ctx.getFilesDir(), "pylibs");
        File certFile = new File(pyUsr, "etc/tls/cert.pem");

        Map<String, String> env = pb.environment();
        env.put("LD_LIBRARY_PATH",
            pyUsr.getAbsolutePath() + "/lib"
            + File.pathSeparator
            + ffUsr.getAbsolutePath() + "/lib");
        env.put("PYTHONHOME", pyUsr.getAbsolutePath());
        env.put("PYTHONPATH", pylibs.getAbsolutePath());
        env.put("TMPDIR", ctx.getCacheDir().getAbsolutePath());
        if (certFile.exists()) {
            env.put("SSL_CERT_FILE", certFile.getAbsolutePath());
        }
    }

    /**
     * Runs ytmusic_search.py under the bundled libpython.so. Returns stdout
     * (JSON-per-line matching DownloadManager::_parseSearchOutput()). Empty
     * string on failure — caller falls back to yt-dlp search.
     */
    public static String runYtMusicSearch(Context ctx, String query, int limit) {
        if (!ytmusicapiReady) ensureYtmusicapi(ctx);
        if (!ytmusicapiReady) {
            lastExitCode = -1;
            return "";
        }
        String python = getPythonBinaryPath(ctx);
        String script = extractSearchScript(ctx);
        if (python == null || script == null) {
            lastExitCode = -1;
            return "";
        }

        try {
            ProcessBuilder pb = new ProcessBuilder(
                python, script, query, String.valueOf(limit));
            applyPythonEnv(ctx, pb);
            pb.redirectErrorStream(false);
            Process p = pb.start();

            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            try (InputStream stdout = p.getInputStream()) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = stdout.read(buf)) > 0) bos.write(buf, 0, n);
            }
            // Drain stderr to logcat so import/network errors aren't silent.
            try (InputStream stderr = p.getErrorStream()) {
                ByteArrayOutputStream ebos = new ByteArrayOutputStream();
                byte[] buf = new byte[4096];
                int n;
                while ((n = stderr.read(buf)) > 0) ebos.write(buf, 0, n);
                if (ebos.size() > 0) {
                    Log.w(TAG, "ytmusic_search.py stderr: " + ebos.toString("UTF-8"));
                }
            }
            if (!p.waitFor(30, TimeUnit.SECONDS)) {
                p.destroy();
                lastExitCode = -2;
                return "";
            }
            lastExitCode = p.exitValue();
            if (lastExitCode != 0) {
                Log.w(TAG, "ytmusic_search.py exit=" + lastExitCode);
            }
            return bos.toString("UTF-8");
        } catch (Throwable t) {
            Log.e(TAG, "runYtMusicSearch failed", t);
            lastExitCode = -2;
            return "";
        }
    }

    /**
     * Replaces the cover-art atom of an m4a/mp4 file with the given image
     * bytes using mutagen (which ships in youtubedl-android's Python
     * site-packages). Returns true on success. Used post-download on Android
     * to swap yt-dlp's 16:9 YouTube thumbnail for the square ytmusicapi cover
     * we already cached during search — the bundled ffmpeg has no -vf filters
     * so we can't crop during embed.
     */
    public static boolean runEmbedCover(Context ctx, String audioPath, String coverPath) {
        String python = getPythonBinaryPath(ctx);
        String script = extractScriptAsset(ctx, COVER_SCRIPT_ASSET, "embed_cover.py");
        if (python == null || script == null || audioPath == null || coverPath == null) {
            return false;
        }
        if (!new File(audioPath).exists() || !new File(coverPath).exists()) {
            return false;
        }

        try {
            ProcessBuilder pb = new ProcessBuilder(python, script, audioPath, coverPath);
            applyPythonEnv(ctx, pb);
            pb.redirectErrorStream(true);
            Process p = pb.start();
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            try (InputStream in = p.getInputStream()) {
                byte[] buf = new byte[4096];
                int n;
                while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
            }
            if (!p.waitFor(15, TimeUnit.SECONDS)) {
                p.destroy();
                Log.w(TAG, "embed_cover.py timed out");
                return false;
            }
            int exit = p.exitValue();
            if (exit != 0) {
                Log.w(TAG, "embed_cover.py exit=" + exit
                    + " output=" + bos.toString("UTF-8"));
                return false;
            }
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "runEmbedCover failed", t);
            return false;
        }
    }

    private static String safeOut(String s) { return s == null ? "" : s; }

    private static void deleteRecursive(File f) {
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) for (File k : kids) deleteRecursive(k);
        }
        f.delete();
    }
}
