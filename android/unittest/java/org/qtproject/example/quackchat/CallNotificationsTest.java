package org.qtproject.example.quackchat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.media.AudioManager;
import android.os.Vibrator;
import android.os.VibratorManager;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;

/** The ringing notification is silent; the ring is CallRinger's, tied to it. */
@RunWith(RobolectricTestRunner.class)
public class CallNotificationsTest {
    private static final String ACC = "me@example.org";

    private final Context ctx = RuntimeEnvironment.getApplication();
    private NotificationManager nm;
    private Vibrator vibrator;

    @Before
    public void setUp() {
        nm = ctx.getSystemService(NotificationManager.class);
        vibrator = ctx.getSystemService(VibratorManager.class).getDefaultVibrator();
        shadowOf(vibrator).setHasVibrator(true);
        ctx.getSystemService(AudioManager.class).setRingerMode(AudioManager.RINGER_MODE_NORMAL);
    }

    private boolean ringing() {
        Frames.idle();
        return shadowOf(vibrator).isVibrating();
    }

    private static CallRegistry.Call incoming() {
        return new CallRegistry.Call(ACC, Frames.sid(), "peer@example.org", false,
                "incoming", false);
    }

    private Notification posted(String channel) {
        for (Notification n : shadowOf(nm).getAllNotifications()) {
            if (channel.equals(n.getChannelId())) {
                return n;
            }
        }
        return null;
    }

    @Test
    public void theIncomingChannelMakesNoSoundOfItsOwn() {
        new CallNotifications(ctx);
        final NotificationChannel ch = nm.getNotificationChannel("calls_incoming_2");
        assertNotNull(ch);
        assertNull(ch.getSound());
        assertFalse(ch.shouldVibrate());
        assertEquals(NotificationManager.IMPORTANCE_HIGH, ch.getImportance());
    }

    @Test
    public void theOldRingingChannelIsRemoved() {
        nm.createNotificationChannel(new NotificationChannel(
                "calls_incoming_1", "old", NotificationManager.IMPORTANCE_HIGH));
        new CallNotifications(ctx);
        assertNull(nm.getNotificationChannel("calls_incoming_1"));
    }

    @Test
    public void anIncomingCallRingsUntilItIsCancelled() {
        final CallNotifications notes = new CallNotifications(ctx);
        final CallRegistry.Call c = incoming();
        notes.showIncoming(c);
        assertNotNull(posted("calls_incoming_2"));
        assertTrue(ringing());
        notes.cancelIncoming(c);
        assertNull(posted("calls_incoming_2"));
        assertFalse(ringing());
    }

    @Test
    public void silencingStopsTheRingButKeepsTheCall() {
        final CallNotifications notes = new CallNotifications(ctx);
        final CallRegistry.Call c = incoming();
        notes.showIncoming(c);
        notes.silence(c);
        assertFalse(ringing());
        assertNotNull(posted("calls_incoming_2"));
        // An update to the silenced call, e.g. its avatar, rings no more.
        notes.showIncoming(c);
        assertFalse(ringing());
        notes.cancelIncoming(c);
    }

    @Test
    public void aGroupInviteRingsUntilAnswered() {
        final CallNotifications notes = new CallNotifications(ctx);
        final String room = "call-" + Frames.sid() + "@muc.example.org";
        notes.onGroupInvite(Frames.event("groupcall", "Invited", Frames.args(
                "acc", ACC, "jid", room, "chat", "room@conference.example.org",
                "from", "Alice", "timestamp", 5L, "video", false)));
        assertNotNull(posted("calls_incoming_2"));
        assertTrue(ringing());
        notes.cancelGroupInvite(ACC, room);
        assertFalse(ringing());
    }

    @Test
    public void theServiceGoingAwaySilencesEverything() {
        final CallNotifications notes = new CallNotifications(ctx);
        notes.showIncoming(incoming());
        notes.stopRinging();
        assertFalse(ringing());
    }
}
