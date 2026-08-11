package org.qtproject.example.quackchat;

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
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.nio.charset.StandardCharsets;

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

    private FrameRouter router;
    private Notifications notifications;

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
        router = new FrameRouter(SOCKET_NAME, (module, name, frame) -> {
            // tacky decides what deserves an alert; this only presents it.
            if ("notify".equals(module) && "Notify".equals(name)) {
                notifications.onNotify(frame);
            } else if ("message".equals(module) && "OwnRead".equals(name)) {
                notifications.onOwnRead(frame);
            }
        });

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
        return new String[] {
            "-transient", "0", // persist, so an enabled account reconnects on its own
            "-config-dir", config.getAbsolutePath(),
            "-data-dir", data.getAbsolutePath(),
            "-cache-dir", cache.getAbsolutePath(),
        };
    }

    private void goForeground() {
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

    @Override
    public IBinder onBind(Intent intent) {
        return null; // the UI attaches over the socket, not through a binder
    }

    @Override
    public void onDestroy() {
        if (router != null) {
            router.stop();
        }
        super.onDestroy();
    }
}
