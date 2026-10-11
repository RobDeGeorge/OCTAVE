package org.octave.phoneserver.control;

import org.octave.phoneserver.FakeContext;
import org.octave.phoneserver.device.Device;
import org.octave.phoneserver.util.Ln;

import android.annotation.SuppressLint;
import android.content.ComponentName;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;
import android.view.KeyEvent;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.Objects;

/**
 * OCTAVE extension: report the phone's active media session (what is
 * playing, play state, position, album art) to the client and drive it from
 * OCTAVE's media controls, so phone audio behaves like another media source.
 * <p>
 * Sessions come from the media_session service through its binder (the shell
 * holds MEDIA_CONTENT_CONTROL). The followed session is the one playing, or
 * else the most recently active; it is re-picked every second, and its
 * MediaController callbacks report changes as they happen. Every change is
 * sent as a TYPE_OCTAVE_MEDIA_STATE device message, album art (a scaled
 * JPEG) as TYPE_OCTAVE_MEDIA_ART when it changes, and the session's play
 * queue (Spotify: the current track and the next 49) as
 * TYPE_OCTAVE_MEDIA_QUEUE when it or its playing item changes. Queue items
 * carry their art URI; each distinct one is read from the app's provider
 * (Spotify: content://com.spotify.mobile.android.mediaapi/...) on a
 * separate thread and sent once per session as TYPE_OCTAVE_MEDIA_QUEUE_ART.
 */
public final class PhoneMedia {

    private static final long PICK_MS = 1000;
    private static final int ART_MAX_PX = 400;
    private static final int ART_QUALITY = 85;
    private static final long PAUSE_WAIT_MS = 500;
    public static final int QUEUE_MAX_ITEMS = 500;
    private static final int QUEUE_ART_PX = 160;
    private static final int QUEUE_ART_QUALITY = 80;

    /** What the client is told; immutable once sent. */
    public static final class State {
        public boolean hasSession;
        public boolean playing;
        public boolean canSeek;
        public long positionMs;
        public long durationMs;
        public String packageName = "";
        public String title = "";
        public String artist = "";
        public String album = "";

        boolean sameAs(State o) {
            return o != null && hasSession == o.hasSession && playing == o.playing && canSeek == o.canSeek
                    && durationMs == o.durationMs && packageName.equals(o.packageName) && title.equals(o.title)
                    && artist.equals(o.artist) && album.equals(o.album);
        }
    }

    public static final class QueueEntry {
        public final long id;
        public final String title;
        public final String subtitle;
        public final String artUri;   // content:// art of the item, "" for none

        QueueEntry(long id, String title, String subtitle, String artUri) {
            this.id = id;
            this.title = title;
            this.subtitle = subtitle;
            this.artUri = artUri;
        }
    }

    /** The followed session's play queue; immutable once sent. */
    public static final class Queue {
        public long activeId = MediaSession.QueueItem.UNKNOWN_ID;
        public String title = "";
        public List<QueueEntry> items = new ArrayList<>();
    }

    private final DeviceMessageSender sender;
    private final HandlerThread thread;
    private final Handler handler;
    // Queue art is read here so a slow provider never holds up state reports
    private final HandlerThread artThread;
    private final Handler artHandler;
    private final Set<String> artRequested = new HashSet<>();   // art URIs read or queued this session (handler thread)

    private boolean watching;
    private MediaController controller;
    private State lastSent;
    private long lastSentAt;           // uptime ms
    private String artKey;             // identity of the art last sent, null = none sent yet
    private boolean artClearPending;   // a "no art" is held back (apps send art after the title)
    private List<QueueEntry> queueItems = new ArrayList<>();   // of the followed session
    private String queueTitle = "";
    private String queueKeySent;       // identity of the queue last sent, null = none sent yet
    private long queueActiveSent;
    private static final long ART_CLEAR_DELAY_MS = 1000;

    private final MediaController.Callback callback = new MediaController.Callback() {
        @Override
        public void onPlaybackStateChanged(PlaybackState state) {
            report(false);
        }

        @Override
        public void onMetadataChanged(MediaMetadata metadata) {
            report(false);
        }

        @Override
        public void onQueueChanged(List<MediaSession.QueueItem> queue) {
            readQueue(queue, controller);
            reportQueue(false);
        }

        @Override
        public void onQueueTitleChanged(CharSequence title) {
            queueTitle = title == null ? "" : title.toString();
            reportQueue(false);
        }

        @Override
        public void onSessionDestroyed() {
            pick();
        }
    };

    private final Runnable pickRunnable = new Runnable() {
        @Override
        public void run() {
            pick();
            handler.postDelayed(this, PICK_MS);
        }
    };

    public PhoneMedia(DeviceMessageSender sender) {
        this.sender = sender;
        thread = new HandlerThread("phone-media");
        thread.start();
        handler = new Handler(thread.getLooper());
        artThread = new HandlerThread("phone-media-art");
        artThread.start();
        artHandler = new Handler(artThread.getLooper());
    }

    public void handle(int action, long arg) {
        handler.post(() -> {
            try {
                doHandle(action, arg);
            } catch (Throwable t) {
                Ln.e("Phone media error", t);
            }
        });
    }

    public void stop() {
        handler.post(() -> {
            watching = false;
            handler.removeCallbacks(pickRunnable);
            setController(null);
        });
        thread.quitSafely();
        artThread.quit();
    }

    // ── handler thread ────────────────────────────────────────────────

    private void doHandle(int action, long arg) {
        if (action == ControlMessage.MEDIA_WATCH) {
            boolean on = arg != 0;
            if (on == watching) {
                return;
            }
            watching = on;
            if (on) {
                lastSent = null;
                artKey = null;
                queueKeySent = null;
                artRequested.clear();   // a new client has none of it
                Ln.i("Phone media: watching the media session");
                pick();
                if (lastSent == null) {
                    report(true);   // no session: say so once
                }
                handler.postDelayed(pickRunnable, PICK_MS);
            } else {
                handler.removeCallbacks(pickRunnable);
                setController(null);
            }
            return;
        }

        MediaController c = controller;
        if (c == null) {
            // No session to talk to: a media key still reaches (and restarts)
            // the last media app
            if (action == ControlMessage.MEDIA_PLAY || action == ControlMessage.MEDIA_PLAY_PAUSE) {
                Device.pressReleaseKeycode(KeyEvent.KEYCODE_MEDIA_PLAY, 0, Device.INJECT_MODE_ASYNC);
            }
            return;
        }
        MediaController.TransportControls tc = c.getTransportControls();
        switch (action) {
            case ControlMessage.MEDIA_PLAY:
                tc.play();
                break;
            case ControlMessage.MEDIA_PAUSE:
                tc.pause();
                break;
            case ControlMessage.MEDIA_PLAY_PAUSE:
                if (isPlaying(c.getPlaybackState())) {
                    tc.pause();
                } else {
                    tc.play();
                }
                break;
            case ControlMessage.MEDIA_NEXT:
                tc.skipToNext();
                break;
            case ControlMessage.MEDIA_PREVIOUS:
                tc.skipToPrevious();
                break;
            case ControlMessage.MEDIA_SEEK:
                tc.seekTo(Math.max(0, arg));
                break;
            case ControlMessage.MEDIA_SKIP_TO_QUEUE_ITEM:
                tc.skipToQueueItem(arg);
                break;
            default:
                break;
        }
    }

    /** Follow the session that is playing, else the most recently active one. */
    private void pick() {
        if (!watching) {
            return;
        }
        List<MediaSession.Token> tokens = getSessions();
        MediaController best = null;
        if (tokens != null) {
            for (MediaSession.Token token : tokens) {
                if (controller != null && controller.getSessionToken().equals(token)) {
                    if (best == null || isPlaying(controller.getPlaybackState())) {
                        best = controller;
                    }
                } else {
                    MediaController mc = new MediaController(FakeContext.get(), token);
                    if (best == null || (isPlaying(mc.getPlaybackState()) && !isPlaying(best.getPlaybackState()))) {
                        best = mc;
                    }
                }
                if (best != null && isPlaying(best.getPlaybackState())) {
                    break;   // the list is in priority order; the first one playing wins
                }
            }
        }
        if (best != controller) {
            // Callbacks report changes within a session; only a switch is reported from here
            setController(best);
            report(false);
        }
    }

    private void setController(MediaController c) {
        if (controller != null) {
            try {
                controller.unregisterCallback(callback);
            } catch (RuntimeException e) {
                // the session is gone
            }
        }
        controller = c;
        List<MediaSession.QueueItem> queue = null;
        CharSequence title = null;
        if (c != null) {
            try {
                queue = c.getQueue();
                title = c.getQueueTitle();
            } catch (RuntimeException e) {
                // the session is gone; the next pick drops it
            }
        }
        readQueue(queue, c);
        queueTitle = title == null ? "" : title.toString();
        if (c != null) {
            Ln.i("Phone media: following " + c.getPackageName());
            try {
                c.registerCallback(callback, handler);
            } catch (RuntimeException e) {
                Ln.w("Phone media: could not register for " + c.getPackageName() + ": " + e.getMessage());
            }
        }
    }

    private List<MediaSession.Token> getSessions() {
        try {
            return querySessions();
        } catch (ReflectiveOperationException | RuntimeException e) {
            Ln.e("Phone media: could not list media sessions", e);
            watching = false;   // no point polling an API that does not work here
            handler.removeCallbacks(pickRunnable);
            return null;
        }
    }

    private static Object sessionManager;     // android.media.session.ISessionManager
    private static Method getSessionsMethod;

    @SuppressLint("PrivateApi")
    @SuppressWarnings("unchecked")
    private static synchronized List<MediaSession.Token> querySessions() throws ReflectiveOperationException {
        if (sessionManager == null) {
            IBinder binder = (IBinder) Class.forName("android.os.ServiceManager").getMethod("getService", String.class)
                    .invoke(null, "media_session");
            Object sm = Class.forName("android.media.session.ISessionManager$Stub").getMethod("asInterface", IBinder.class)
                    .invoke(null, binder);
            for (Method m : sm.getClass().getMethods()) {
                Class<?>[] p = m.getParameterTypes();
                // getSessions(ComponentName, int userId[, String packageFilter]) (the filter since Android 17)
                if (m.getName().equals("getSessions") && p.length >= 2 && p[0] == ComponentName.class && p[1] == int.class) {
                    getSessionsMethod = m;
                    break;
                }
            }
            if (getSessionsMethod == null) {
                throw new NoSuchMethodException("ISessionManager.getSessions");
            }
            sessionManager = sm;
        }
        if (getSessionsMethod.getParameterTypes().length == 3) {
            return (List<MediaSession.Token>) getSessionsMethod.invoke(sessionManager, null, 0, null);   // every package
        }
        return (List<MediaSession.Token>) getSessionsMethod.invoke(sessionManager, null, 0);
    }

    /**
     * Pause every session that is playing. Called when OCTAVE stops capturing
     * the phone's audio (the client closed, crashed or dropped): the audio
     * would otherwise fall back to the phone's own speaker and keep playing
     * out loud. Falls back to a media pause key if sessions cannot be listed.
     */
    public static void pausePlaying() {
        try {
            List<MediaSession.Token> tokens = querySessions();
            List<MediaController> paused = new java.util.ArrayList<>();
            if (tokens != null) {
                for (MediaSession.Token token : tokens) {
                    MediaController mc = new MediaController(FakeContext.get(), token);
                    if (isPlaying(mc.getPlaybackState())) {
                        Ln.i("Phone media: pausing " + mc.getPackageName() + " (audio leaving OCTAVE)");
                        mc.getTransportControls().pause();
                        paused.add(mc);
                    }
                }
            }
            // The app pauses asynchronously: hold the capture a moment so the
            // tail of the track does not leak out of the speaker
            long deadline = SystemClock.uptimeMillis() + PAUSE_WAIT_MS;
            while (!paused.isEmpty() && SystemClock.uptimeMillis() < deadline) {
                paused.removeIf(mc -> !isPlaying(mc.getPlaybackState()));
                SystemClock.sleep(20);
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            Ln.w("Phone media: could not pause sessions, sending a pause key: " + e.getMessage());
            Device.pressReleaseKeycode(KeyEvent.KEYCODE_MEDIA_PAUSE, 0, Device.INJECT_MODE_WAIT_FOR_FINISH);
        }
    }

    private static boolean isPlaying(PlaybackState ps) {
        if (ps == null) {
            return false;
        }
        int s = ps.getState();
        return s == PlaybackState.STATE_PLAYING || s == PlaybackState.STATE_BUFFERING || s == PlaybackState.STATE_FAST_FORWARDING
                || s == PlaybackState.STATE_REWINDING || s == PlaybackState.STATE_SKIPPING_TO_NEXT
                || s == PlaybackState.STATE_SKIPPING_TO_PREVIOUS;
    }

    /** Send the state if anything changed (or force), and the art if it changed. */
    private void report(boolean force) {
        if (!watching) {
            return;
        }
        MediaController c = controller;
        State st = new State();
        MediaMetadata md = null;
        if (c != null) {
            PlaybackState ps;
            try {
                ps = c.getPlaybackState();
                md = c.getMetadata();
            } catch (RuntimeException e) {
                setController(null);
                report(force);
                return;
            }
            st.hasSession = true;
            st.packageName = c.getPackageName();
            st.playing = isPlaying(ps);
            if (ps != null) {
                st.canSeek = (ps.getActions() & PlaybackState.ACTION_SEEK_TO) != 0;
                long pos = ps.getPosition();
                if (st.playing && ps.getLastPositionUpdateTime() > 0) {
                    pos += (long) ((SystemClock.elapsedRealtime() - ps.getLastPositionUpdateTime()) * ps.getPlaybackSpeed());
                }
                st.positionMs = Math.max(0, pos);
            }
            if (md != null) {
                st.title = str(md, MediaMetadata.METADATA_KEY_TITLE, MediaMetadata.METADATA_KEY_DISPLAY_TITLE);
                st.artist = str(md, MediaMetadata.METADATA_KEY_ARTIST, MediaMetadata.METADATA_KEY_ALBUM_ARTIST);
                st.album = str(md, MediaMetadata.METADATA_KEY_ALBUM, null);
                st.durationMs = Math.max(0, md.getLong(MediaMetadata.METADATA_KEY_DURATION));
                if (st.durationMs > 0) {
                    st.positionMs = Math.min(st.positionMs, st.durationMs);
                }
            }
        }

        sendArt(md);
        reportQueue(false);   // the playing item moves with the playback state

        long now = SystemClock.uptimeMillis();
        // Position is extrapolated by the client while playing; resync it
        // when it jumps (a seek)
        boolean positionJumped = lastSent != null
                && Math.abs(expectedPosition(lastSent, now - lastSentAt) - st.positionMs) > 1500;
        if (force || !st.sameAs(lastSent) || positionJumped) {
            lastSent = st;
            lastSentAt = now;
            sender.send(DeviceMessage.createOctaveMediaState(st));
        }
    }

    private void readQueue(List<MediaSession.QueueItem> queue, MediaController from) {
        if (from != controller) {
            return;   // a callback from a session no longer followed
        }
        List<QueueEntry> items = new ArrayList<>();
        if (queue != null) {
            for (MediaSession.QueueItem item : queue) {
                if (items.size() >= QUEUE_MAX_ITEMS) {
                    break;
                }
                CharSequence t = item.getDescription().getTitle();
                CharSequence s = item.getDescription().getSubtitle();
                Uri icon = item.getDescription().getIconUri();
                String art = icon != null && "content".equals(icon.getScheme()) ? icon.toString() : "";
                items.add(new QueueEntry(item.getQueueId(), t == null ? "" : t.toString(), s == null ? "" : s.toString(), art));
            }
        }
        queueItems = items;
    }

    /** Send the queue if it or its playing item changed (or force). */
    private void reportQueue(boolean force) {
        if (!watching) {
            return;
        }
        long activeId = MediaSession.QueueItem.UNKNOWN_ID;
        MediaController c = controller;
        if (c != null) {
            try {
                PlaybackState ps = c.getPlaybackState();
                if (ps != null) {
                    activeId = ps.getActiveQueueItemId();
                }
            } catch (RuntimeException e) {
                // the session is gone
            }
        }
        StringBuilder key = new StringBuilder(queueTitle);
        for (QueueEntry e : queueItems) {
            key.append('\n').append(e.id).append('\t').append(e.title).append('\t').append(e.subtitle);
        }
        String k = key.toString();
        if (!force && k.equals(queueKeySent) && activeId == queueActiveSent) {
            return;
        }
        queueKeySent = k;
        queueActiveSent = activeId;
        Queue q = new Queue();
        q.activeId = activeId;
        q.title = queueTitle;
        q.items = queueItems;
        sender.send(DeviceMessage.createOctaveMediaQueue(q));
        requestQueueArt(queueItems);
    }

    /** Queue the art of items the client has not been sent yet, in queue order. */
    private void requestQueueArt(List<QueueEntry> items) {
        for (QueueEntry e : items) {
            if (e.artUri.isEmpty() || !artRequested.add(e.artUri)) {
                continue;
            }
            final String uri = e.artUri;
            artHandler.post(() -> sender.sendBulk(DeviceMessage.createOctaveMediaQueueArt(uri, readArt(uri))));
        }
    }

    /** The art at a content URI as a small JPEG; empty if it cannot be read. */
    private static byte[] readArt(String uri) {
        try (InputStream in = FakeContext.get().getContentResolver().openInputStream(Uri.parse(uri))) {
            Bitmap bmp = in == null ? null : BitmapFactory.decodeStream(in);
            if (bmp == null) {
                return new byte[0];
            }
            int w = bmp.getWidth(), h = bmp.getHeight();
            if (Math.max(w, h) > QUEUE_ART_PX) {
                float f = (float) QUEUE_ART_PX / Math.max(w, h);
                bmp = Bitmap.createScaledBitmap(bmp, Math.max(1, Math.round(w * f)), Math.max(1, Math.round(h * f)), true);
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            bmp.compress(Bitmap.CompressFormat.JPEG, QUEUE_ART_QUALITY, out);
            return out.toByteArray();
        } catch (Exception e) {
            Ln.w("Phone media: could not read queue art " + uri + ": " + e.getMessage());
            return new byte[0];
        }
    }

    private static long expectedPosition(State st, long elapsed) {
        return st.playing ? st.positionMs + elapsed : st.positionMs;
    }

    private static String str(MediaMetadata md, String key, String fallbackKey) {
        CharSequence v = md.getText(key);
        if ((v == null || v.length() == 0) && fallbackKey != null) {
            v = md.getText(fallbackKey);
        }
        return v == null ? "" : v.toString();
    }

    private void sendArt(MediaMetadata md) {
        Bitmap bmp = null;
        if (md != null) {
            bmp = md.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART);
            if (bmp == null) {
                bmp = md.getBitmap(MediaMetadata.METADATA_KEY_ART);
            }
            if (bmp == null) {
                bmp = md.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON);
            }
        }
        String key = bmp == null ? "" : artKey(bmp);
        if (Objects.equals(key, artKey)) {
            artClearPending = false;   // the art came back (same album)
            return;
        }
        if (bmp == null && artKey != null && !artKey.isEmpty() && controller != null) {
            // A track change usually sends the metadata first and the art a
            // moment later: keep the old art up rather than blanking it.
            if (!artClearPending) {
                artClearPending = true;
                handler.postDelayed(() -> {
                    if (artClearPending) {
                        artClearPending = false;
                        artKey = "";
                        sender.send(DeviceMessage.createOctaveMediaArt(new byte[0]));
                    }
                }, ART_CLEAR_DELAY_MS);
            }
            return;
        }
        artClearPending = false;
        artKey = key;
        byte[] jpeg = new byte[0];
        if (bmp != null) {
            try {
                Bitmap scaled = bmp;
                int w = bmp.getWidth(), h = bmp.getHeight();
                if (Math.max(w, h) > ART_MAX_PX) {
                    float f = (float) ART_MAX_PX / Math.max(w, h);
                    scaled = Bitmap.createScaledBitmap(bmp, Math.max(1, Math.round(w * f)), Math.max(1, Math.round(h * f)), true);
                }
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                scaled.compress(Bitmap.CompressFormat.JPEG, ART_QUALITY, out);
                jpeg = out.toByteArray();
            } catch (RuntimeException e) {
                Ln.w("Phone media: could not encode album art: " + e.getMessage());
            }
        }
        sender.send(DeviceMessage.createOctaveMediaArt(jpeg));
    }

    /** Cheap identity: size plus a grid of sampled pixels (the bitmap is a fresh copy every time). */
    private static String artKey(Bitmap bmp) {
        int w = bmp.getWidth(), h = bmp.getHeight();
        StringBuilder sb = new StringBuilder().append(w).append('x').append(h);
        if (bmp.getConfig() == Bitmap.Config.HARDWARE) {
            bmp = bmp.copy(Bitmap.Config.ARGB_8888, false);
        }
        if (bmp != null && w > 0 && h > 0) {
            for (int i = 1; i <= 4; i++) {
                for (int j = 1; j <= 4; j++) {
                    sb.append(':').append(Integer.toHexString(bmp.getPixel(w * i / 5, h * j / 5)));
                }
            }
        }
        return sb.toString();
    }
}
