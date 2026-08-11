package org.qtproject.example.quackchat;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.RemoteInput;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.qtproject.qt.android.bindings.QtActivity;

import java.nio.charset.StandardCharsets;

/**
 * Turns tacky's notify events into Android notifications.
 *
 * <p>Almost none of the policy lives here. tacky decides what is worth
 * interrupting the user over: {@code notify <Notify>} already accounts for the
 * read watermark (so a chat being read never alerts, with no "which chat is on
 * screen" call), stays silent for the opening archive fetch, caps an offline
 * catch-up at ten per chat, and applies the per-chat mute/mentions policy.
 * {@code unread} is the chat total, so a burst is one notification reading
 * "Ann (12)" rather than twelve.
 *
 * <p>This is also the only place that looks inside a payload. Everything else
 * the router touches is forwarded on its envelope alone; these two events are
 * small, fixed-shape and carry no blobs.
 */
final class Notifications {
    private static final String TAG = "quack.notify";

    private static final String CHANNEL = "messages";
    /** One id for all chats, with the chat as the tag, so each gets its own. */
    private static final int MESSAGE_ID = 100;

    static final String ACTION_REPLY = "org.qtproject.example.quackchat.REPLY";
    static final String ACTION_MARK_READ = "org.qtproject.example.quackchat.MARK_READ";
    static final String EXTRA_ACC = "acc";
    static final String EXTRA_JID = "jid";
    static final String EXTRA_TS = "ts";
    static final String KEY_REPLY = "reply";

    /**
     * Asks for POST_NOTIFICATIONS from the UI process, where the context is the
     * activity. Qt's public permission API does not cover this one, and pulling
     * in QtCore's private Android headers for it is not worth it.
     *
     * <p>The answer is never collected: if it is refused, the service still
     * runs and {@link #onNotify} simply catches the SecurityException.
     */
    static void requestPermission(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return; // granted at install time before 13
        }
        final String perm = Manifest.permission.POST_NOTIFICATIONS;
        if (ctx.checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED) {
            return;
        }
        if (ctx instanceof Activity) {
            ((Activity) ctx).requestPermissions(new String[] {perm}, 0);
        } else {
            Log.w(TAG, "no activity to ask for notification permission");
        }
    }

    private final Context ctx;
    private final NotificationManager nm;

    Notifications(Context ctx) {
        this.ctx = ctx;
        this.nm = ctx.getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(
                CHANNEL, ctx.getString(R.string.messages_channel_name),
                NotificationManager.IMPORTANCE_HIGH));
    }

    /** notify &lt;Notify&gt; {acc, jid, timestamp, nick, unread, mention}. */
    void onNotify(byte[] frame) {
        final JSONObject a = args(frame);
        if (a == null) {
            return;
        }
        final String acc = a.optString("acc");
        final String jid = a.optString("jid");
        if (acc.isEmpty() || jid.isEmpty()) {
            return;
        }
        final String nick = a.optString("nick", jid);
        final int unread = a.optInt("unread", 1);
        final long ts = a.optLong("timestamp");

        final String title = unread > 1 ? nick + " (" + unread + ")" : nick;
        final Notification n = new Notification.Builder(ctx, CHANNEL)
                .setContentTitle(title)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setWhen(ts / 1000)
                .setAutoCancel(true)
                .setContentIntent(openChat(acc, jid))
                .addAction(replyAction(acc, jid))
                .addAction(markReadAction(acc, jid, ts))
                .build();
        try {
            nm.notify(tag(acc, jid), MESSAGE_ID, n);
        } catch (SecurityException e) {
            // POST_NOTIFICATIONS refused. The service keeps running; the user
            // simply sees nothing until they grant it.
            Log.w(TAG, "not allowed to post", e);
        }
    }

    /**
     * message &lt;OwnRead&gt; {acc, jid, timestamp} - the watermark moved, here or
     * on another device, so whatever was shown for this chat is stale.
     */
    void onOwnRead(byte[] frame) {
        final JSONObject a = args(frame);
        if (a == null) {
            return;
        }
        final String acc = a.optString("acc");
        final String jid = a.optString("jid");
        if (!acc.isEmpty() && !jid.isEmpty()) {
            nm.cancel(tag(acc, jid), MESSAGE_ID);
        }
    }

    private static String tag(String acc, String jid) {
        return acc + " " + jid;
    }

    /** The args object of ["event",module,name,args]; null if it is not one. */
    private static JSONObject args(byte[] frame) {
        try {
            final JSONArray arr =
                    new JSONArray(new String(frame, StandardCharsets.UTF_8));
            return arr.optJSONObject(3);
        } catch (JSONException e) {
            Log.w(TAG, "unparseable event", e);
            return null;
        }
    }

    private PendingIntent openChat(String acc, String jid) {
        final Intent i = new Intent(ctx, QtActivity.class)
                .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP)
                .putExtra(EXTRA_ACC, acc)
                .putExtra(EXTRA_JID, jid);
        return PendingIntent.getActivity(ctx, tag(acc, jid).hashCode(), i,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    /**
     * Both actions target the service, not an activity: that is what lets a
     * reply be sent with no UI process alive at all.
     */
    private Notification.Action replyAction(String acc, String jid) {
        final PendingIntent pi = servicePending(ACTION_REPLY, acc, jid, 0);
        return new Notification.Action.Builder(
                        Icon.createWithResource(ctx, android.R.drawable.ic_menu_send),
                        ctx.getString(R.string.action_reply), pi)
                .addRemoteInput(new RemoteInput.Builder(KEY_REPLY)
                        .setLabel(ctx.getString(R.string.action_reply))
                        .build())
                .setAllowGeneratedReplies(false)
                .build();
    }

    private Notification.Action markReadAction(String acc, String jid, long ts) {
        return new Notification.Action.Builder(
                        Icon.createWithResource(ctx, android.R.drawable.ic_menu_view),
                        ctx.getString(R.string.action_mark_read),
                        servicePending(ACTION_MARK_READ, acc, jid, ts))
                .build();
    }

    private PendingIntent servicePending(String action, String acc, String jid,
                                         long ts) {
        final Intent i = new Intent(ctx, QuackBackendService.class)
                .setAction(action)
                .putExtra(EXTRA_ACC, acc)
                .putExtra(EXTRA_JID, jid)
                .putExtra(EXTRA_TS, ts);
        // Mutable: RemoteInput has to write the typed text into it.
        final int flags = ACTION_REPLY.equals(action)
                ? PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
                : PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT;
        return PendingIntent.getService(
                ctx, (action + tag(acc, jid)).hashCode(), i, flags);
    }
}
