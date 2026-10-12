package org.qtproject.example.quackchat;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.ArrayList;
import java.util.List;

@RunWith(RobolectricTestRunner.class)
public class CallRegistryTest {
    private static final String ACC = "me@example.org";

    @Test
    public void outgoingCallWalksItsStates() {
        final String sid = Frames.sid();
        Frames.calls("Outgoing", "acc", ACC, "sid", sid, "to", "peer@example.org", "video", true);
        final CallRegistry.Call c = CallRegistry.get().find(ACC, sid);
        assertNotNull(c);
        assertEquals("calling", c.state);
        assertEquals("peer@example.org", c.peer);
        assertTrue(c.video);
        assertTrue(c.live());

        Frames.calls("Ringing", "acc", ACC, "sid", sid);
        assertEquals("ringing", c.state);
        Frames.calls("Warning", "acc", ACC, "sid", sid, "reason", "media path interrupted");
        Frames.calls("Active", "acc", ACC, "sid", sid, "since", 42L);
        assertEquals("active", c.state);
        assertEquals(42L, c.since);
        assertEquals("", c.warning); // <Active> is the only end of a warning

        Frames.calls("Ended", "acc", ACC, "sid", sid, "reason", "hangup");
        assertEquals("ended", c.state);
        assertEquals("hangup", c.reason);
        assertNull("an ended call leaves the registry", CallRegistry.get().find(ACC, sid));
    }

    @Test
    public void incomingCallRingsHereUntilAccepted() {
        final String sid = Frames.sid();
        Frames.calls("Incoming", "acc", ACC, "sid", sid, "from", "peer@example.org");
        final CallRegistry.Call c = CallRegistry.get().find(ACC, sid);
        assertTrue(c.ringingHere());
        assertFalse("no mic before the answer", c.live());

        CallRegistry.get().accepted(c);
        assertEquals("connecting", c.state);
        assertTrue(c.live());
        Frames.calls("Ended", "acc", ACC, "sid", sid);
    }

    @Test
    public void videoStreamsComeAndGo() {
        final String sid = Frames.sid();
        Frames.calls("Outgoing", "acc", ACC, "sid", sid, "to", "peer@example.org");
        final CallRegistry.Call c = CallRegistry.get().find(ACC, sid);
        Frames.calls("VideoTrack", "acc", ACC, "sid", sid, "name", "remote-1");
        Frames.calls("VideoPreview", "acc", ACC, "sid", sid, "name", "preview-1");
        assertTrue(c.video);
        assertEquals("remote-1", c.remoteStream);
        assertEquals("preview-1", c.previewStream);
        Frames.calls("VideoEnded", "acc", ACC, "sid", sid);
        assertNull(c.remoteStream);
        Frames.calls("Ended", "acc", ACC, "sid", sid);
    }

    @Test
    public void eventsForUnknownSidsAreIgnored() {
        final List<CallRegistry.Call> seen = new ArrayList<>();
        final CallRegistry.Listener l = seen::add;
        CallRegistry.get().addListener(l);
        try {
            // A group call's leg: tacky never announces it as Outgoing/Incoming.
            Frames.calls("Active", "acc", ACC, "sid", Frames.sid());
            Frames.calls("Ended", "acc", ACC, "sid", Frames.sid());
            assertTrue(seen.isEmpty());
        } finally {
            CallRegistry.get().removeListener(l);
        }
    }

    @Test
    public void malformedFramesAreDropped() {
        CallRegistry.get().onEvent("Outgoing", "not json".getBytes());
        Frames.calls("Outgoing", "sid", Frames.sid()); // no acc
        Frames.idle();
        for (CallRegistry.Call c : CallRegistry.get().current()) {
            assertFalse(c.acc.isEmpty());
        }
    }
}
