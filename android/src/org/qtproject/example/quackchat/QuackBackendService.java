package org.qtproject.example.quackchat;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.RemoteInput;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Base64;
import android.util.Log;

import io.github.pounceandmiss.quack.R;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Holds the XMPP session in a process that outlives the UI. Qt ends the
 * activity's process from QtActivityBase.onDestroy() - terminateQtNativeApplication()
 * then System.exit(0) - so a connection living there cannot survive a task
 * swipe, and Qt refuses to run a QtService in that process anyway. This runs
 * under android:process=":backend" with no Qt in it at all.
 */
public final class QuackBackendService extends Service {
    private static final String TAG = "quack.service";

    /** Abstract-namespace name; AppController.cpp connects to the same string. */
    static final String SOCKET_NAME = "quackchat.backend";

    private static final String CHANNEL = "backend";
    private static final int NOTIF_ID = 1;

    static final String ACTION_CALL_DECLINE = "org.qtproject.example.quackchat.CALL_DECLINE";
    static final String ACTION_CALL_HANG_UP = "org.qtproject.example.quackchat.CALL_HANG_UP";
    static final String ACTION_GROUP_DECLINE = "org.qtproject.example.quackchat.GROUP_DECLINE";
    static final String ACTION_GROUP_LEAVE = "org.qtproject.example.quackchat.GROUP_LEAVE";

    /** For the call screens, which run in this process. Null while down. */
    private static volatile FrameRouter liveRouter;
    private static volatile CallAudio liveAudio;
    private static volatile CallNotifications liveCallNotifications;

    static FrameRouter router() {
        return liveRouter;
    }

    static CallAudio audio() {
        return liveAudio;
    }

    static CallNotifications callNotifications() {
        return liveCallNotifications;
    }

    private FrameRouter router;
    private Notifications notifications;
    private CallNotifications callNotifications;
    private CallAudio callAudio;
    /** A CallRegistry.Call or GroupCallRegistry.Call, or null. */
    private Object foregroundCall;
    private final CallRegistry.Listener callListener = this::onCallChanged;
    private final GroupCallRegistry.Listener groupListener = this::onGroupCallChanged;
    private final java.util.Set<String> groupScreensShown = new java.util.HashSet<>();

    /**
     * Brings the service up from the UI process. Called through JNI so the
     * intent handling stays on this side.
     *
     * <p>Both calls are needed. startForegroundService() is refused from the
     * background on Android 12+, and bindService(BIND_AUTO_CREATE) is the
     * fallback that still creates the service - which then promotes itself in
     * onCreate(). Binding without unbinding would tie the service's life to
     * ours, so the binding is dropped again immediately; the service stays up
     * because it is started and foreground.
     */
    public static void start(Context ctx) {
        final Intent intent = new Intent(ctx, QuackBackendService.class);
        try {
            ctx.startForegroundService(intent);
        } catch (RuntimeException e) {
            Log.w(TAG, "startForegroundService refused, binding instead", e);
            final ServiceConnection conn = new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder binder) {}

                @Override
                public void onServiceDisconnected(ComponentName name) {}
            };
            if (ctx.bindService(intent, conn, Context.BIND_AUTO_CREATE)) {
                ctx.unbindService(conn);
            }
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        // Before anything else: the window from startForegroundService() to
        // startForeground() is five seconds, and missing it is a hard crash.
        // Unlike tacky_android this is guarded - a sticky restart lands here
        // from the background, which Android 12+ can refuse outright.
        try {
            goForeground();
        } catch (RuntimeException e) {
            Log.e(TAG, "could not enter the foreground", e);
        }

        notifications = new Notifications(this);
        callNotifications = new CallNotifications(this);
        callAudio = new CallAudio(this);
        liveAudio = callAudio;
        liveCallNotifications = callNotifications;
        CallRegistry.get().addListener(callListener);
        GroupCallRegistry.get().addListener(groupListener);
        router = new FrameRouter(SOCKET_NAME, (module, name, frame) -> {
            // tacky decides what deserves an alert; this only presents it.
            if ("notify".equals(module) && "Notify".equals(name)) {
                notifications.onNotify(frame);
            } else if ("notify".equals(module) && "MissedCall".equals(name)) {
                callNotifications.onMissedCall(frame);
            } else if ("message".equals(module) && "OwnRead".equals(name)) {
                notifications.onOwnRead(frame);
            } else if ("calls".equals(module)) {
                CallRegistry.get().onEvent(name, frame);
            } else if ("groupcall".equals(module) && "State".equals(name)) {
                GroupCallRegistry.get().onState(frame);
            } else if ("groupcall".equals(module) && "Invited".equals(name)) {
                callNotifications.onGroupInvite(frame);
            }
        });
        liveRouter = router;

        // tacky_create returns once requests can be queued, but it still opens
        // a database on the way; keep it off the thread holding the FGS clock.
        new Thread(() -> {
            if (!router.start(tacoArgs())) {
                stopSelf();
            }
        }, "quack-start").start();
    }

    /**
     * taco takes its directories as options, so nothing here depends on HOME or
     * the XDG variables being set for this process.
     */
    private String[] tacoArgs() {
        final File base = new File(getFilesDir(), "tacky");
        final File config = new File(base, "config");
        final File data = new File(base, "data");
        final File cache = new File(base, "cache");
        for (File d : new File[] {base, config, data, cache}) {
            if (!d.exists() && !d.mkdirs()) {
                Log.w(TAG, "could not create " + d);
            }
        }
        final List<String> args = new ArrayList<>(Arrays.asList(
            "-transient", "0", // persist, so an enabled account reconnects on its own
            "-config-dir", config.getAbsolutePath(),
            "-data-dir", data.getAbsolutePath(),
            "-cache-dir", cache.getAbsolutePath()));
        if (loadWebrtc()) {
            args.addAll(Arrays.asList(
                "-media-backend", "webrtc", "-webrtc-lib", "libtacky_webrtc.so"));
        }
        return args.toArray(new String[0]);
    }

    /**
     * The webrtc media backend, when the apk carries it. Loaded here so its
     * JNI_OnLoad runs; libwebrtc's Java audio finds the app through ContextUtils.
     * tacky then loads the same library by name.
     */
    private boolean loadWebrtc() {
        try {
            System.loadLibrary("tacky_webrtc");
            Class.forName("org.webrtc.ContextUtils")
                .getMethod("initialize", Context.class)
                .invoke(null, getApplicationContext());
            return true;
        } catch (UnsatisfiedLinkError | ReflectiveOperationException e) {
            Log.i(TAG, "no webrtc media backend: " + e);
            return false;
        }
    }

    private void goForeground() {
        foregroundCall = null;
        final NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(
                CHANNEL, getString(R.string.backend_channel_name),
                NotificationManager.IMPORTANCE_LOW));

        final Intent open = new Intent(this, QtActivity.class)
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        final Notification n = new Notification.Builder(this, CHANNEL)
                .setContentTitle(getString(R.string.backend_notification_title))
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setContentIntent(PendingIntent.getActivity(
                        this, 0, open,
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
                .setOngoing(true)
                .build();

        // specialUse is the only type that fits. dataSync is capped at six
        // hours per day from Android 15 and then throws, and remoteMessaging
        // means device-to-device handoff, not a client's own connection.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIF_ID, n);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        handleAction(intent);
        return START_STICKY;
    }

    // --- calls -------------------------------------------------------------

    private void onCallChanged(CallRegistry.Call c) {
        if (c.ringingHere()) {
            if (c.name == null) {
                lookUp(c);
            }
            callNotifications.showIncoming(c);
            return;
        }
        callNotifications.cancelIncoming(c);
        if (c.outgoing && "calling".equals(c.state) && c.name == null) {
            // Placed from the app, which is on screen, so we may start an
            // activity and claim the microphone.
            lookUp(c);
            startActivity(CallActivity.intent(this, c, CallActivity.ACTION_SHOW));
        }
        refreshForeground();
    }

    /** Joins begin in the app, on screen, as outgoing 1:1 calls do. */
    private void onGroupCallChanged(GroupCallRegistry.Call g) {
        final String key = GroupCallRegistry.key(g.acc, g.room);
        if (g.live()) {
            callNotifications.cancelGroupInvite(g.acc, g.room);
            if (groupScreensShown.add(key)) {
                startActivity(GroupCallActivity.intent(this, g.acc, g.room));
            }
        } else {
            groupScreensShown.remove(key);
        }
        refreshForeground();
    }

    /** The foreground notification: a 1:1 call, else a group call, else the connection. */
    private void refreshForeground() {
        Object want = null;
        boolean video = false;
        for (CallRegistry.Call c : CallRegistry.get().current()) {
            if (c.live()) {
                want = c;
                video = c.video;
                break;
            }
        }
        if (want == null) {
            for (GroupCallRegistry.Call g : GroupCallRegistry.get().current()) {
                want = g;
                video = g.video;
                break;
            }
        }
        if (want == null) {
            if (foregroundCall != null) {
                foregroundCall = null;
                callAudio.stop();
                demote();
            }
            return;
        }
        final Notification n = want instanceof CallRegistry.Call
                ? callNotifications.ongoing((CallRegistry.Call) want)
                : callNotifications.ongoingGroup((GroupCallRegistry.Call) want);
        if (want == foregroundCall) {
            getSystemService(NotificationManager.class).notify(NOTIF_ID, n);
            return;
        }
        callAudio.start(video);
        promote(want, n, video);
    }

    /**
     * Without the microphone type Android silences the mic once the app is off
     * screen, and the type can only be claimed while the app is visible.
     */
    private void promote(Object call, Notification n, boolean video) {
        foregroundCall = call;
        // specialUse only exists from 14; before it, the service had no type.
        final int base = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
                ? ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE : 0;
        int types = base;
        if (granted(Manifest.permission.RECORD_AUDIO)) {
            types |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE;
        }
        if (video && granted(Manifest.permission.CAMERA)) {
            types |= ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA;
        }
        try {
            startForeground(NOTIF_ID, n, types);
        } catch (RuntimeException e) {
            // Not visible after all: the mic goes when the screen does.
            Log.w(TAG, "could not claim the microphone for the call", e);
            startForeground(NOTIF_ID, n, base);
        }
    }

    private void demote() {
        try {
            goForeground();
        } catch (RuntimeException e) {
            Log.e(TAG, "could not return to the plain foreground", e);
        }
    }

    private boolean granted(String permission) {
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    /** The peer's name and avatar, best effort. */
    private void lookUp(CallRegistry.Call c) {
        c.name = "";
        try {
            router.request("author", "get",
                    new JSONObject().put("acc", c.acc).put("chat", c.peer),
                    (result, error) -> {
                        if (result instanceof JSONObject) {
                            final String name = ((JSONObject) result).optString(c.peer);
                            if (!name.isEmpty()) {
                                c.name = name;
                                main(() -> CallRegistry.get().changed(c));
                            }
                        }
                    });
            router.request("avatar", "metadata",
                    new JSONObject().put("acc", c.acc).put("jid", c.peer),
                    (meta, error) -> {
                        if (!(meta instanceof JSONObject)) {
                            return;
                        }
                        final String hash = ((JSONObject) meta).optString("hash");
                        if (hash.isEmpty()) {
                            return;
                        }
                        try {
                            router.request("avatar", "data",
                                    new JSONObject().put("acc", c.acc).put("hash", hash),
                                    (data, err) -> {
                                        final Bitmap bmp = decodeAvatar(data);
                                        if (bmp != null) {
                                            c.avatar = bmp;
                                            main(() -> CallRegistry.get().changed(c));
                                        }
                                    });
                        } catch (JSONException e) {
                            Log.w(TAG, "avatar request", e);
                        }
                    });
        } catch (JSONException e) {
            Log.w(TAG, "call lookups", e);
        }
    }

    private static Bitmap decodeAvatar(Object data) {
        if (!(data instanceof String) || ((String) data).isEmpty()) {
            return null;
        }
        try {
            final byte[] bytes = Base64.decode((String) data, Base64.DEFAULT);
            return BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    private void main(Runnable r) {
        new android.os.Handler(android.os.Looper.getMainLooper()).post(r);
    }

    /**
     * Notification buttons arrive here rather than at an activity, so they work
     * with no UI process alive. Each becomes one tokenless notify frame: tacky
     * routes replies by token and the UI is the only allocator of those.
     */
    private void handleAction(Intent intent) {
        if (intent == null || intent.getAction() == null || router == null) {
            return;
        }
        final String acc = intent.getStringExtra(Notifications.EXTRA_ACC);
        final String room = intent.getStringExtra(GroupCallActivity.EXTRA_ROOM);
        if (acc != null && room != null) {
            try {
                switch (intent.getAction()) {
                    case ACTION_GROUP_DECLINE:
                        callNotifications.cancelGroupInvite(acc, room);
                        router.inject("groupcall", "decline", new JSONObject().put("acc", acc)
                                .put("chat", intent.getStringExtra(GroupCallActivity.EXTRA_CHAT))
                                .put("timestamp", intent.getLongExtra(GroupCallActivity.EXTRA_TS, 0)));
                        return;
                    case ACTION_GROUP_LEAVE:
                        router.inject("groupcall", "leave",
                                new JSONObject().put("acc", acc).put("jid", room));
                        return;
                    default:
                        break;
                }
            } catch (JSONException e) {
                Log.w(TAG, "groupcall action", e);
                return;
            }
        }
        final String sid = intent.getStringExtra(CallActivity.EXTRA_SID);
        if (acc != null && sid != null) {
            switch (intent.getAction()) {
                case ACTION_CALL_DECLINE:
                    callFrame("reject", acc, sid);
                    return;
                case ACTION_CALL_HANG_UP:
                    callFrame("hangup", acc, sid);
                    return;
                default:
                    break;
            }
        }
        final String jid = intent.getStringExtra(Notifications.EXTRA_JID);
        if (acc == null || jid == null) {
            return;
        }
        switch (intent.getAction()) {
            case Notifications.ACTION_REPLY: {
                final Bundle input = RemoteInput.getResultsFromIntent(intent);
                final CharSequence body =
                        input == null ? null : input.getCharSequence(Notifications.KEY_REPLY);
                if (body != null && body.length() > 0) {
                    router.inject(frame("message", "send", acc, jid,
                            "body", body.toString()));
                }
                break;
            }
            case Notifications.ACTION_MARK_READ: {
                final long ts = intent.getLongExtra(Notifications.EXTRA_TS, 0);
                if (ts > 0) {
                    // The resulting <OwnRead> is what clears the notification,
                    // so there is nothing to cancel here.
                    router.inject(frame("message", "markOwnRead", acc, jid,
                            "timestamp", ts));
                }
                break;
            }
            default:
                break;
        }
    }

    /**
     * ["module","method",{acc,chat,key}] - three elements, so no token. Every
     * write from here is about one chat on one account plus a single value.
     */
    private static byte[] frame(String module, String method, String acc,
                                String jid, String key, Object value) {
        try {
            final JSONObject o = new JSONObject()
                    .put("acc", acc).put("chat", jid).put(key, value);
            return new JSONArray().put(module).put(method).put(o).toString()
                    .getBytes(StandardCharsets.UTF_8);
        } catch (JSONException e) {
            throw new IllegalArgumentException("cannot build " + module, e);
        }
    }

    private void callFrame(String method, String acc, String sid) {
        try {
            router.inject("calls", method, new JSONObject().put("acc", acc).put("sid", sid));
        } catch (JSONException e) {
            Log.w(TAG, "calls " + method, e);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null; // the UI attaches over the socket, not through a binder
    }

    @Override
    public void onDestroy() {
        CallRegistry.get().removeListener(callListener);
        GroupCallRegistry.get().removeListener(groupListener);
        liveRouter = null;
        liveAudio = null;
        liveCallNotifications = null;
        if (callNotifications != null) {
            callNotifications.stopRinging();
        }
        if (callAudio != null) {
            callAudio.stop();
        }
        if (router != null) {
            router.stop();
        }
        super.onDestroy();
    }
}
