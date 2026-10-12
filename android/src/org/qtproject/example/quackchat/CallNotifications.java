package org.qtproject.example.quackchat;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Person;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.media.AudioAttributes;
import android.media.RingtoneManager;
import android.os.Build;
import android.util.Log;

import io.github.pounceandmiss.quack.R;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

/**
 * Ringing, ongoing and missed call notifications. As in Conversations, the
 * ring is the incoming notification's own channel sound, repeated by
 * FLAG_INSISTENT, so the system applies ringer mode and DND.
 */
final class CallNotifications {
    private static final String TAG = "quack.callnotify";

    /** Versioned: a channel's sound can't change once created. */
    private static final String CHANNEL_INCOMING = "calls_incoming_1";
    static final String CHANNEL_ONGOING = "calls_ongoing";
    private static final String CHANNEL_MISSED = "calls_missed";

    private static final int INCOMING_ID = 200;
    private static final int GROUP_INVITE_ID = 201;
    private static final int MISSED_ID = 300;

    /** A group invite has no "ring over" event, so it times out. */
    private static final long GROUP_RING_MS = 45_000;

    /** Conversations' pattern. */
    private static final long[] VIBRATION = {0, 500, 300, 600, 3000};

    private final Context ctx;
    private final NotificationManager nm;
    /** Rings the volume keys silenced. */
    private final Set<String> silenced = new HashSet<>();

    CallNotifications(Context ctx) {
        this.ctx = ctx;
        this.nm = ctx.getSystemService(NotificationManager.class);

        final NotificationChannel incoming = new NotificationChannel(
                CHANNEL_INCOMING, ctx.getString(R.string.calls_incoming_channel_name),
                NotificationManager.IMPORTANCE_HIGH);
        incoming.setSound(RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE),
                new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build());
        incoming.enableVibration(true);
        incoming.setVibrationPattern(VIBRATION);
        incoming.setBypassDnd(true);
        incoming.setShowBadge(false);
        incoming.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
        nm.createNotificationChannel(incoming);

        final NotificationChannel ongoing = new NotificationChannel(
                CHANNEL_ONGOING, ctx.getString(R.string.calls_ongoing_channel_name),
                NotificationManager.IMPORTANCE_LOW);
        ongoing.setShowBadge(false);
        nm.createNotificationChannel(ongoing);

        nm.createNotificationChannel(new NotificationChannel(
                CHANNEL_MISSED, ctx.getString(R.string.calls_missed_channel_name),
                NotificationManager.IMPORTANCE_HIGH));
    }

    private static String tag(String acc, String sid) {
        return acc + " " + sid;
    }

    void showIncoming(CallRegistry.Call c) {
        if (Build.VERSION.SDK_INT >= 34 && !nm.canUseFullScreenIntent()) {
            Log.w(TAG, "no full-screen intent permission: ringing as a heads-up only");
        }
        final PendingIntent screen = CallActivity.pending(ctx, c, CallActivity.ACTION_SHOW);
        final PendingIntent answer = CallActivity.pending(ctx, c, CallActivity.ACTION_ANSWER);
        final PendingIntent decline = servicePending(QuackBackendService.ACTION_CALL_DECLINE, c);
        final Person caller = person(c);

        // A silenced ring moves to the quiet channel.
        final boolean quiet = silenced.contains(tag(c.acc, c.sid));
        final Notification.Builder b = new Notification.Builder(ctx,
                        quiet ? CHANNEL_ONGOING : CHANNEL_INCOMING)
                .setSmallIcon(R.drawable.ic_call)
                .setContentTitle(c.displayName())
                .setContentText(ctx.getString(c.video ? R.string.call_incoming_video
                                                      : R.string.call_incoming))
                .setCategory(Notification.CATEGORY_CALL)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setFullScreenIntent(screen, true)
                .setContentIntent(screen)
                .addPerson(caller);
        if (c.avatar != null) {
            b.setLargeIcon(c.avatar);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setStyle(Notification.CallStyle.forIncomingCall(caller, decline, answer));
        } else {
            b.addAction(action(R.drawable.ic_call_end, R.string.call_decline, decline))
             .addAction(action(R.drawable.ic_call, R.string.call_answer, answer));
        }
        final Notification n = b.build();
        if (!quiet) {
            n.flags |= Notification.FLAG_INSISTENT;
        }
        post(tag(c.acc, c.sid), INCOMING_ID, n);
    }

    void silence(CallRegistry.Call c) {
        if (!silenced.add(tag(c.acc, c.sid))) {
            return;
        }
        nm.cancel(tag(c.acc, c.sid), INCOMING_ID);
        showIncoming(c);
    }

    void cancelIncoming(CallRegistry.Call c) {
        silenced.remove(tag(c.acc, c.sid));
        nm.cancel(tag(c.acc, c.sid), INCOMING_ID);
    }

    /** Posted by the service as its foreground notification, as CallStyle requires. */
    Notification ongoing(CallRegistry.Call c) {
        final PendingIntent screen = CallActivity.pending(ctx, c, CallActivity.ACTION_SHOW);
        final PendingIntent hangUp = servicePending(QuackBackendService.ACTION_CALL_HANG_UP, c);
        final Person peer = person(c);
        final Notification.Builder b = new Notification.Builder(ctx, CHANNEL_ONGOING)
                .setSmallIcon(c.video ? R.drawable.ic_videocam : R.drawable.ic_call)
                .setContentTitle(c.displayName())
                .setContentText(ctx.getString(c.video ? R.string.call_ongoing_video
                                                      : R.string.call_ongoing))
                .setCategory(Notification.CATEGORY_CALL)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(screen)
                .addPerson(peer);
        if (c.since > 0) {
            b.setWhen(c.since / 1000).setShowWhen(true).setUsesChronometer(true);
        }
        if (c.avatar != null) {
            b.setLargeIcon(c.avatar);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setStyle(Notification.CallStyle.forOngoingCall(peer, hangUp));
        } else {
            b.addAction(action(R.drawable.ic_call_end, R.string.call_hang_up, hangUp));
        }
        return b.build();
    }

    /** groupcall &lt;Invited&gt; {acc, jid, from, chat, timestamp, video}. */
    void onGroupInvite(byte[] frame) {
        final JSONObject a;
        try {
            a = new JSONArray(new String(frame, StandardCharsets.UTF_8)).optJSONObject(3);
        } catch (JSONException e) {
            Log.w(TAG, "unparseable group invite", e);
            return;
        }
        if (a == null) {
            return;
        }
        final GroupCallActivity.Invite inv = new GroupCallActivity.Invite(
                a.optString("acc"), a.optString("jid"), a.optString("chat"),
                a.optLong("timestamp"), a.optString("from"), a.optBoolean("video"));
        if (inv.acc.isEmpty() || inv.room.isEmpty() || inv.chat.isEmpty()) {
            return;
        }
        final PendingIntent screen =
                GroupCallActivity.pendingInvite(ctx, inv, GroupCallActivity.ACTION_SHOW);
        final PendingIntent join =
                GroupCallActivity.pendingInvite(ctx, inv, GroupCallActivity.ACTION_ANSWER);
        final Intent no = new Intent(ctx, QuackBackendService.class)
                .setAction(QuackBackendService.ACTION_GROUP_DECLINE)
                .putExtra(Notifications.EXTRA_ACC, inv.acc)
                .putExtra(GroupCallActivity.EXTRA_ROOM, inv.room)
                .putExtra(GroupCallActivity.EXTRA_CHAT, inv.chat)
                .putExtra(GroupCallActivity.EXTRA_TS, inv.timestamp);
        final PendingIntent decline = PendingIntent.getService(ctx,
                (QuackBackendService.ACTION_GROUP_DECLINE + inv.acc + inv.room).hashCode(), no,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        final Person caller = new Person.Builder().setName(inv.from).setKey(inv.from)
                .setUri("xmpp:" + inv.from).setImportant(true).build();
        final Notification.Builder b = new Notification.Builder(ctx, CHANNEL_INCOMING)
                .setSmallIcon(R.drawable.ic_call)
                .setContentTitle(inv.chat)
                .setContentText(ctx.getString(inv.video ? R.string.group_call_invite_video
                                                        : R.string.group_call_invite, inv.from))
                .setCategory(Notification.CATEGORY_CALL)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setTimeoutAfter(GROUP_RING_MS)
                .setFullScreenIntent(screen, true)
                .setContentIntent(screen)
                .addPerson(caller);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setStyle(Notification.CallStyle.forIncomingCall(caller, decline, join));
        } else {
            b.addAction(action(R.drawable.ic_call_end, R.string.call_decline, decline))
             .addAction(action(R.drawable.ic_call, R.string.group_call_join, join));
        }
        final Notification n = b.build();
        n.flags |= Notification.FLAG_INSISTENT;
        post(tag(inv.acc, inv.room), GROUP_INVITE_ID, n);
    }

    void cancelGroupInvite(String acc, String room) {
        nm.cancel(tag(acc, room), GROUP_INVITE_ID);
    }

    Notification ongoingGroup(GroupCallRegistry.Call c) {
        final PendingIntent screen = GroupCallActivity.pending(ctx, c.acc, c.room);
        final Intent out = new Intent(ctx, QuackBackendService.class)
                .setAction(QuackBackendService.ACTION_GROUP_LEAVE)
                .putExtra(Notifications.EXTRA_ACC, c.acc)
                .putExtra(GroupCallActivity.EXTRA_ROOM, c.room);
        final PendingIntent leave = PendingIntent.getService(ctx,
                (QuackBackendService.ACTION_GROUP_LEAVE + c.acc + c.room).hashCode(), out,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        final Person group = new Person.Builder().setName(c.title()).setKey(c.room)
                .setImportant(true).build();
        final Notification.Builder b = new Notification.Builder(ctx, CHANNEL_ONGOING)
                .setSmallIcon(c.video ? R.drawable.ic_videocam : R.drawable.ic_call)
                .setContentTitle(c.title())
                .setContentText(ctx.getString(R.string.group_call_count, c.count))
                .setCategory(Notification.CATEGORY_CALL)
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(screen);
        if (c.since > 0) {
            b.setWhen(c.since / 1000).setShowWhen(true).setUsesChronometer(true);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setStyle(Notification.CallStyle.forOngoingCall(group, leave));
        } else {
            b.addAction(action(R.drawable.ic_call_end, R.string.group_call_leave, leave));
        }
        return b.build();
    }

    /** notify &lt;MissedCall&gt; {acc, jid, timestamp, nick, video}. */
    void onMissedCall(byte[] frame) {
        final JSONObject a;
        try {
            a = new JSONArray(new String(frame, StandardCharsets.UTF_8)).optJSONObject(3);
        } catch (JSONException e) {
            Log.w(TAG, "unparseable missed call", e);
            return;
        }
        if (a == null) {
            return;
        }
        final String acc = a.optString("acc");
        final String jid = a.optString("jid");
        if (acc.isEmpty() || jid.isEmpty()) {
            return;
        }
        String nick = a.optString("nick");
        if (nick.isEmpty()) {
            nick = jid;
        }
        final boolean video = a.optBoolean("video");
        final Notification n = new Notification.Builder(ctx, CHANNEL_MISSED)
                .setSmallIcon(android.R.drawable.stat_notify_missed_call)
                .setContentTitle(nick)
                .setContentText(ctx.getString(video ? R.string.call_missed_video
                                                    : R.string.call_missed))
                .setCategory(Notification.CATEGORY_MISSED_CALL)
                .setWhen(a.optLong("timestamp") / 1000)
                .setShowWhen(true)
                .setAutoCancel(true)
                .setContentIntent(Notifications.openChat(ctx, acc, jid))
                .build();
        // One per caller.
        post(acc + " " + jid, MISSED_ID, n);
    }

    private Person person(CallRegistry.Call c) {
        final Person.Builder p = new Person.Builder()
                .setName(c.displayName())
                .setKey(c.peer)
                .setUri("xmpp:" + c.peer)
                .setImportant(true);
        if (c.avatar != null) {
            p.setIcon(Icon.createWithBitmap(c.avatar));
        }
        return p.build();
    }

    private Notification.Action action(int icon, int label, PendingIntent pi) {
        return new Notification.Action.Builder(
                Icon.createWithResource(ctx, icon), ctx.getString(label), pi).build();
    }

    private PendingIntent servicePending(String action, CallRegistry.Call c) {
        final Intent i = new Intent(ctx, QuackBackendService.class)
                .setAction(action)
                .putExtra(Notifications.EXTRA_ACC, c.acc)
                .putExtra(CallActivity.EXTRA_SID, c.sid);
        return PendingIntent.getService(ctx, (action + tag(c.acc, c.sid)).hashCode(), i,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private void post(String tag, int id, Notification n) {
        try {
            nm.notify(tag, id, n);
        } catch (SecurityException e) {
            Log.w(TAG, "not allowed to post", e);
        }
    }
}
