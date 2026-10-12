package org.qtproject.example.quackchat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.view.View;

import io.github.pounceandmiss.quack.R;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.qtproject.qt.android.bindings.QtActivity;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.android.controller.ActivityController;

/** The group call screen: the same ways out as the 1:1 one, from groupcall <State>. */
@RunWith(RobolectricTestRunner.class)
public class GroupCallActivityTest {
    private static final String ACC = "me@example.org";
    private static final String CHAT = "room@conference.example.org";

    private final Context ctx = RuntimeEnvironment.getApplication();
    /** The call's own room, as a hosted call has: not the chat. */
    private String room;
    private ActivityController<GroupCallActivity> controller;

    @After
    public void leave() {
        if (room != null) {
            Frames.groupState(state("ended", false));
            Frames.groupState(state("idle", false));
        }
    }

    private JSONObject state(String phase, boolean video) {
        final JSONObject peer = Frames.args("nick", "Alice", "jid", "alice@example.org",
                "joined", true, "leg", "active", "stream", video ? "alice-video" : "");
        return Frames.args("acc", ACC, "jid", room, "chat", CHAT, "phase", phase,
                "video", video, "camera", video, "count", 2, "since", 1L,
                "participants", new JSONArray().put(peer));
    }

    private void inCall(boolean video) {
        room = "call-" + Frames.sid() + "@muc.example.org";
        Frames.groupState(state("live", video));
    }

    private GroupCallActivity launch() {
        controller = Robolectric.buildActivity(GroupCallActivity.class,
                new Intent(ctx, GroupCallActivity.class)
                        .setAction(GroupCallActivity.ACTION_SHOW)
                        .putExtra(Notifications.EXTRA_ACC, ACC)
                        .putExtra(GroupCallActivity.EXTRA_ROOM, room))
                .setup();
        return controller.get();
    }

    private static boolean shown(GroupCallActivity a, int id) {
        return a.findViewById(id).getVisibility() == View.VISIBLE;
    }

    private static void assertOpensChat(Intent i) {
        assertNotNull("the chat was opened", i);
        assertEquals(QtActivity.class.getName(), i.getComponent().getClassName());
        assertEquals(ACC, i.getStringExtra(Notifications.EXTRA_ACC));
        assertEquals("the chat, not the call's room", CHAT,
                i.getStringExtra(Notifications.EXTRA_JID));
    }

    @Test
    public void backFromAVoiceCallOpensTheRoomsChat() {
        inCall(false);
        final GroupCallActivity a = launch();
        assertTrue(shown(a, R.id.to_chat));
        a.onBackPressed();
        assertOpensChat(shadowOf(a).getNextStartedActivity());
        assertTrue(a.isFinishing());
    }

    @Test
    public void backFromAVideoCallFloats() {
        inCall(true);
        final GroupCallActivity a = launch();
        a.onBackPressed();
        assertTrue(a.isInPictureInPictureMode());
        assertOpensChat(shadowOf(a).getNextStartedActivity());
        assertFalse(a.isFinishing());
    }

    @Test
    public void anInviteIsNotLeftByBack() {
        room = null; // nothing joined: an invite only
        final String inviteRoom = "call-" + Frames.sid() + "@muc.example.org";
        controller = Robolectric.buildActivity(GroupCallActivity.class,
                new Intent(ctx, GroupCallActivity.class)
                        .setAction(GroupCallActivity.ACTION_SHOW)
                        .putExtra(Notifications.EXTRA_ACC, ACC)
                        .putExtra(GroupCallActivity.EXTRA_ROOM, inviteRoom)
                        .putExtra(GroupCallActivity.EXTRA_CHAT, CHAT)
                        .putExtra(GroupCallActivity.EXTRA_TS, 5L)
                        .putExtra(GroupCallActivity.EXTRA_FROM, "Alice"))
                .setup();
        final GroupCallActivity a = controller.get();
        assertTrue(shown(a, R.id.invite_row));
        assertFalse(shown(a, R.id.to_chat));
        a.onBackPressed();
        assertNull(shadowOf(a).getNextStartedActivity());
        assertTrue(a.isFinishing());
    }

    @Test
    public void floatingShowsTheGridAlone() {
        inCall(true);
        final GroupCallActivity a = launch();
        controller.pause();
        a.onPictureInPictureModeChanged(true, new Configuration());
        assertFalse(shown(a, R.id.title));
        assertFalse(shown(a, R.id.call_row));
        assertFalse(shown(a, R.id.to_chat));
        assertTrue(shown(a, R.id.grid_frame));
    }

    @Test
    public void swipingTheWindowAwayClosesTheScreenNotTheCall() {
        inCall(true);
        final GroupCallActivity a = launch();
        controller.pause();
        a.onPictureInPictureModeChanged(true, new Configuration());
        controller.stop();
        a.onPictureInPictureModeChanged(false, new Configuration());
        assertTrue(a.isFinishing());
        assertTrue(GroupCallRegistry.get().find(ACC, room).live());
    }

    @Test
    public void expandingTheWindowKeepsTheScreen() {
        inCall(true);
        final GroupCallActivity a = launch();
        controller.pause();
        a.onPictureInPictureModeChanged(true, new Configuration());
        a.onPictureInPictureModeChanged(false, new Configuration());
        controller.resume();
        controller.pause().stop();
        assertFalse(a.isFinishing());
        assertTrue(shown(a, R.id.call_row));
    }
}
