package org.qtproject.example.quackchat;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.app.NotificationManager;
import android.content.Context;
import android.media.AudioManager;
import android.os.Looper;
import android.os.Vibrator;
import android.os.VibratorManager;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;

import java.time.Duration;

/**
 * When the ring sounds and when it stops. The vibration stands in for the
 * whole ring: it starts in every mode that rings at all.
 */
@RunWith(RobolectricTestRunner.class)
public class CallRingerTest {
    private final Context ctx = RuntimeEnvironment.getApplication();
    private AudioManager am;
    private NotificationManager nm;
    private Vibrator vibrator;
    private CallRinger ringer;

    @Before
    public void setUp() {
        am = ctx.getSystemService(AudioManager.class);
        nm = ctx.getSystemService(NotificationManager.class);
        vibrator = ctx.getSystemService(VibratorManager.class).getDefaultVibrator();
        shadowOf(vibrator).setHasVibrator(true);
        shadowOf(nm).setNotificationPolicyAccessGranted(true);
        am.setRingerMode(AudioManager.RINGER_MODE_NORMAL);
        ringer = new CallRinger(ctx);
    }

    private boolean ringing() {
        return shadowOf(vibrator).isVibrating();
    }

    private void ring(String key) {
        ringer.ring(key, 0);
        Frames.idle();
    }

    private void stop(String key) {
        ringer.stop(key);
        Frames.idle();
    }

    @Test
    public void ringsUntilStopped() {
        ring("a");
        assertTrue(ringing());
        stop("a");
        assertFalse(ringing());
    }

    @Test
    public void ringsWhileAnyCallStillWantsIt() {
        ring("a");
        ring("b");
        stop("a");
        assertTrue(ringing());
        stop("b");
        assertFalse(ringing());
    }

    @Test
    public void aSecondRingForTheSameCallIsOne() {
        ring("a");
        ring("a"); // the notification is reposted on every change
        stop("a");
        assertFalse(ringing());
    }

    @Test
    public void stoppingACallThatNeverRangIsHarmless() {
        ring("a");
        stop("b");
        assertTrue(ringing());
        stop("a");
    }

    @Test
    public void silentModeKeepsQuiet() {
        am.setRingerMode(AudioManager.RINGER_MODE_SILENT);
        ring("a");
        assertFalse(ringing());
        stop("a");
    }

    @Test
    public void vibrateModeOnlyVibrates() {
        am.setRingerMode(AudioManager.RINGER_MODE_VIBRATE);
        ring("a");
        assertTrue(ringing());
        stop("a");
        assertFalse(ringing());
    }

    @Test
    public void doNotDisturbKeepsQuiet() {
        nm.setInterruptionFilter(NotificationManager.INTERRUPTION_FILTER_NONE);
        ring("a");
        assertFalse(ringing());
        stop("a");
    }

    @Test
    public void priorityModeLetsCallsFromAnyoneThrough() {
        nm.setNotificationPolicy(new NotificationManager.Policy(
                NotificationManager.Policy.PRIORITY_CATEGORY_CALLS,
                NotificationManager.Policy.PRIORITY_SENDERS_ANY, 0));
        nm.setInterruptionFilter(NotificationManager.INTERRUPTION_FILTER_PRIORITY);
        ring("a");
        assertTrue(ringing());
        stop("a");
    }

    @Test
    public void priorityModeForContactsOnlyKeepsQuiet() {
        nm.setNotificationPolicy(new NotificationManager.Policy(
                NotificationManager.Policy.PRIORITY_CATEGORY_CALLS,
                NotificationManager.Policy.PRIORITY_SENDERS_CONTACTS, 0));
        nm.setInterruptionFilter(NotificationManager.INTERRUPTION_FILTER_PRIORITY);
        ring("a");
        assertFalse(ringing());
        stop("a");
    }

    @Test
    public void aGroupInviteStopsRingingByItself() {
        ringer.ring("invite", 45_000);
        Frames.idle();
        assertTrue(ringing());
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(44));
        assertTrue(ringing());
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(2));
        assertFalse(ringing());
    }

    @Test
    public void anAnsweredInviteDoesNotStopALaterRing() {
        ringer.ring("invite", 45_000);
        Frames.idle();
        stop("invite");
        ring("call");
        // The invite's timeout went with it, so it cannot cut this ring short.
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(46));
        assertTrue(ringing());
        stop("call");
    }

    @Test
    public void stopAllSilencesEverything() {
        ring("a");
        ring("b");
        ringer.stopAll();
        Frames.idle();
        assertFalse(ringing());
    }
}
