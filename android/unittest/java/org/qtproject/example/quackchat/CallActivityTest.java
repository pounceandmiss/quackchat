package org.qtproject.example.quackchat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.app.KeyguardManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.view.View;

import io.github.pounceandmiss.quack.R;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.qtproject.qt.android.bindings.QtActivity;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.android.controller.ActivityController;

import java.time.Duration;

/**
 * The 1:1 call screen's lifecycle: leaving for the chat, floating, and the
 * ways the floating window ends. The call itself is tacky's, fed in as events.
 */
@RunWith(RobolectricTestRunner.class)
public class CallActivityTest {
    private static final String ACC = "me@example.org";
    private static final String PEER = "peer@example.org";

    private final Context ctx = RuntimeEnvironment.getApplication();
    private String sid;
    private ActivityController<CallActivity> controller;

    @After
    public void endCall() {
        if (sid != null) {
            Frames.calls("Ended", "acc", ACC, "sid", sid);
        }
    }

    private void voiceCall() {
        sid = Frames.sid();
        Frames.calls("Outgoing", "acc", ACC, "sid", sid, "to", PEER);
        Frames.calls("Active", "acc", ACC, "sid", sid, "since", 1L);
    }

    private void videoCall() {
        sid = Frames.sid();
        Frames.calls("Outgoing", "acc", ACC, "sid", sid, "to", PEER, "video", true);
        Frames.calls("VideoTrack", "acc", ACC, "sid", sid, "name", "remote");
        Frames.calls("VideoPreview", "acc", ACC, "sid", sid, "name", "preview");
        Frames.calls("Active", "acc", ACC, "sid", sid, "since", 1L);
    }

    private CallActivity launch() {
        final Intent i = new Intent(ctx, CallActivity.class)
                .setAction(CallActivity.ACTION_SHOW)
                .putExtra(Notifications.EXTRA_ACC, ACC)
                .putExtra(CallActivity.EXTRA_SID, sid);
        controller = Robolectric.buildActivity(CallActivity.class, i).setup();
        return controller.get();
    }

    /** Where the activity sent the user, or null. */
    private Intent started(CallActivity a) {
        return shadowOf(a).getNextStartedActivity();
    }

    private static void assertOpensChat(Intent i) {
        assertNotNull("the chat was opened", i);
        assertEquals(QtActivity.class.getName(), i.getComponent().getClassName());
        assertEquals(ACC, i.getStringExtra(Notifications.EXTRA_ACC));
        assertEquals(PEER, i.getStringExtra(Notifications.EXTRA_JID));
    }

    private static boolean shown(CallActivity a, int id) {
        return a.findViewById(id).getVisibility() == View.VISIBLE;
    }

    private void floating(CallActivity a) {
        controller.pause();
        a.onPictureInPictureModeChanged(true, new Configuration());
    }

    @Test
    public void aCallAlreadyOverClosesTheScreen() {
        sid = Frames.sid(); // never announced
        final CallActivity a = launch();
        assertTrue(a.isFinishing());
        sid = null;
    }

    @Test
    public void backWhileRingingOnlyLeaves() {
        sid = Frames.sid();
        Frames.calls("Incoming", "acc", ACC, "sid", sid, "from", PEER);
        final CallActivity a = launch();
        assertFalse(shown(a, R.id.to_chat));
        a.onBackPressed();
        assertTrue(a.isFinishing());
        assertNull("a ringing call does not open the chat", started(a));
    }

    @Test
    public void backFromAVoiceCallOpensTheChatAndCloses() {
        voiceCall();
        final CallActivity a = launch();
        assertTrue(shown(a, R.id.to_chat));
        a.onBackPressed();
        assertOpensChat(started(a));
        assertTrue(a.isFinishing());
    }

    @Test
    public void theBackButtonDoesWhatBackDoes() {
        voiceCall();
        final CallActivity a = launch();
        a.findViewById(R.id.to_chat).performClick();
        assertOpensChat(started(a));
        assertTrue(a.isFinishing());
    }

    @Test
    public void backFromAVideoCallFloatsOverTheChat() {
        videoCall();
        final CallActivity a = launch();
        a.onBackPressed();
        assertTrue(a.isInPictureInPictureMode());
        assertOpensChat(started(a));
        assertFalse("the floating window is this activity", a.isFinishing());
    }

    @Test
    public void overTheLockScreenTheChatWaitsForTheUnlock() {
        voiceCall();
        final KeyguardManager km = ctx.getSystemService(KeyguardManager.class);
        shadowOf(km).setKeyguardLocked(true);
        final CallActivity a = launch();
        a.onBackPressed();
        assertNull(started(a));
        assertFalse(a.isFinishing());

        shadowOf(km).setKeyguardLocked(false);
        Frames.idle();
        assertOpensChat(started(a));
        assertTrue(a.isFinishing());
    }

    @Test
    public void floatingShowsThePictureAlone() {
        videoCall();
        final CallActivity a = launch();
        assertTrue(shown(a, R.id.controls));
        floating(a);
        assertTrue(shown(a, R.id.remote_video));
        assertFalse(shown(a, R.id.controls));
        assertFalse(shown(a, R.id.video_top));
        assertFalse(shown(a, R.id.preview_video));
        assertFalse(shown(a, R.id.to_chat));
    }

    @Test
    public void floatingWithoutVideoShowsTheName() {
        videoCall();
        final CallActivity a = launch();
        floating(a);
        Frames.calls("VideoEnded", "acc", ACC, "sid", sid);
        assertTrue(shown(a, R.id.pip_label));
        assertFalse(shown(a, R.id.voice_content));
    }

    @Test
    public void swipingTheWindowAwayClosesTheScreenNotTheCall() {
        videoCall();
        final CallActivity a = launch();
        floating(a);
        // Android stops the activity first, then says it left the window.
        controller.stop();
        assertFalse(a.isFinishing());
        a.onPictureInPictureModeChanged(false, new Configuration());
        assertTrue(a.isFinishing());
        assertNotNull("the call goes on", CallRegistry.get().find(ACC, sid));
    }

    @Test
    public void swipingAwayInTheOtherOrderClosesItToo() {
        videoCall();
        final CallActivity a = launch();
        floating(a);
        a.onPictureInPictureModeChanged(false, new Configuration());
        controller.stop();
        assertTrue(a.isFinishing());
    }

    @Test
    public void expandingTheWindowKeepsTheScreen() {
        videoCall();
        final CallActivity a = launch();
        floating(a);
        a.onPictureInPictureModeChanged(false, new Configuration());
        controller.resume();
        assertFalse(a.isFinishing());
        assertTrue(shown(a, R.id.controls));
        // A later trip to the background is not a dismissal.
        controller.pause().stop();
        assertFalse(a.isFinishing());
    }

    @Test
    public void theLockScreenOverTheWindowIsNotADismissal() {
        videoCall();
        final CallActivity a = launch();
        floating(a);
        controller.stop();
        assertFalse(a.isFinishing());
    }

    // Not covered: an activity recreated while floating. Robolectric forgets
    // the picture-in-picture state across a recreate, where Android keeps it.

    @Test
    public void theScreenLingersOnAnEndedCallThenCloses() {
        voiceCall();
        final CallActivity a = launch();
        Frames.calls("Ended", "acc", ACC, "sid", sid, "reason", "hangup");
        sid = null;
        assertFalse(a.isFinishing());
        shadowOf(android.os.Looper.getMainLooper()).idleFor(Duration.ofMillis(1500));
        assertTrue(a.isFinishing());
    }
}
