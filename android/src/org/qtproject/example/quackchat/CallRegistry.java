package org.qtproject.example.quackchat;

import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * The 1:1 calls, from tacky's calls events: the backend process's copy, as
 * CallsModel is the Qt UI's. Group call legs never send &lt;Outgoing&gt; or
 * &lt;Incoming&gt;, so their sids are unknown here and ignored. Listeners run
 * on the main thread.
 */
final class CallRegistry {
    private static final String TAG = "quack.calls";

    static final class Call {
        final String acc;
        final String sid;
        final String peer; // bare
        final boolean outgoing;
        /** calling, ringing, incoming, connecting, active, ended, failed */
        String state;
        /** Offered or asked for at the start. */
        boolean video;
        /** [clock microseconds] of the first &lt;Active&gt;, 0 before. */
        long since;
        /** &lt;Ended&gt;'s reason, or &lt;Failed&gt;'s. */
        String reason = "";
        String warning = "";
        boolean verified;
        /** Frame stream names, null when none. */
        String remoteStream;
        String previewStream;
        /** Looked up by the service; may stay null. */
        String name;
        Bitmap avatar;

        Call(String acc, String sid, String peer, boolean outgoing, String state,
             boolean video) {
            this.acc = acc;
            this.sid = sid;
            this.peer = peer;
            this.outgoing = outgoing;
            this.state = state;
            this.video = video;
        }

        boolean terminal() {
            return "ended".equals(state) || "failed".equals(state);
        }

        boolean ringingHere() {
            return "incoming".equals(state);
        }

        /** The mic is, or is about to be, open. */
        boolean live() {
            return "connecting".equals(state) || "active".equals(state)
                    || (outgoing && !terminal());
        }

        String displayName() {
            return name == null || name.isEmpty() ? peer : name;
        }
    }

    interface Listener {
        void onCallChanged(Call call);
    }

    private static final CallRegistry INSTANCE = new CallRegistry();

    static CallRegistry get() {
        return INSTANCE;
    }

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Call> calls = new LinkedHashMap<>();
    private final List<Listener> listeners = new CopyOnWriteArrayList<>();

    private CallRegistry() {}

    static String key(String acc, String sid) {
        return acc + "\n" + sid;
    }

    void addListener(Listener l) {
        listeners.add(l);
    }

    void removeListener(Listener l) {
        listeners.remove(l);
    }

    /** Main thread only. */
    Call find(String acc, String sid) {
        return calls.get(key(acc, sid));
    }

    /** Main thread only. Every call not yet over. */
    List<Call> current() {
        final List<Call> out = new ArrayList<>();
        for (Call c : calls.values()) {
            if (!c.terminal()) {
                out.add(c);
            }
        }
        return out;
    }

    /** Main thread only. */
    void changed(Call c) {
        for (Listener l : listeners) {
            l.onCallChanged(c);
        }
        if (c.terminal()) {
            calls.remove(key(c.acc, c.sid));
        }
    }

    /** tacky moves no state on accept until media is up. */
    void accepted(Call c) {
        if ("incoming".equals(c.state)) {
            c.state = "connecting";
            changed(c);
        }
    }

    /** On tacky's thread: parsed here, applied on main. */
    void onEvent(String name, byte[] frame) {
        final JSONObject a;
        try {
            a = new JSONArray(new String(frame, StandardCharsets.UTF_8)).optJSONObject(3);
        } catch (JSONException e) {
            Log.w(TAG, "unparseable calls event", e);
            return;
        }
        if (a == null) {
            return;
        }
        main.post(() -> apply(name, a));
    }

    private void apply(String name, JSONObject a) {
        final String acc = a.optString("acc");
        final String sid = a.optString("sid");
        if (acc.isEmpty() || sid.isEmpty()) {
            return;
        }
        if ("Outgoing".equals(name) || "Incoming".equals(name)) {
            final boolean out = "Outgoing".equals(name);
            final Call c = new Call(acc, sid, a.optString(out ? "to" : "from"), out,
                    out ? "calling" : "incoming", a.optBoolean("video"));
            calls.put(key(acc, sid), c);
            changed(c);
            return;
        }
        final Call c = calls.get(key(acc, sid));
        if (c == null) {
            return; // a group call's session, or one that is already over
        }
        switch (name) {
            case "Ringing":
                if ("calling".equals(c.state)) {
                    c.state = "ringing";
                }
                break;
            case "Active":
                c.state = "active";
                c.since = a.optLong("since");
                // The only sign a <Warning> is over.
                c.warning = "";
                break;
            case "Ended":
                c.state = "ended";
                c.reason = a.optString("reason");
                break;
            case "Failed":
                c.state = "failed";
                c.reason = a.optString("reason");
                break;
            case "Warning":
                c.warning = a.optString("reason");
                break;
            case "Verified":
                c.verified = a.optBoolean("verified");
                break;
            case "VideoTrack":
                c.video = true;
                c.remoteStream = a.optString("name", null);
                break;
            case "VideoPreview":
                c.video = true;
                c.previewStream = a.optString("name", null);
                break;
            case "VideoEnded":
                c.remoteStream = null;
                break;
            default:
                return;
        }
        changed(c);
    }
}
