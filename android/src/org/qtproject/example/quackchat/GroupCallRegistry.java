package org.qtproject.example.quackchat;

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
 * The newest groupcall &lt;State&gt; of each call we are joining, in, or just
 * left: the backend process's copy, as GroupCall is the Qt UI's. Idle states
 * are the Qt banner's and ignored here. Listeners run on the main thread.
 */
final class GroupCallRegistry {
    private static final String TAG = "quack.groupcalls";

    static final class Participant {
        String key;
        String nick;
        String jid;     // bare, "" when the room hides it
        boolean joined;
        boolean video;
        /** expected, connecting, active, ended, failed */
        String leg;
        String reason;
        String warning;
        String stream;  // "" without video
    }

    static final class Call {
        final String acc;
        final String room;
        String chat = "";
        boolean hosted;
        /** joining, live, ended */
        String phase = "joining";
        String reason = "";
        String warning = "";
        long since;
        boolean video;
        boolean camera;
        /** The self-view's frame stream, or null. */
        String preview;
        int count;
        List<Participant> participants = new ArrayList<>();

        Call(String acc, String room) {
            this.acc = acc;
            this.room = room;
        }

        boolean live() {
            return "joining".equals(phase) || "live".equals(phase);
        }

        String title() {
            return chat.isEmpty() ? room : chat;
        }
    }

    interface Listener {
        void onGroupCallChanged(Call call);
    }

    private static final GroupCallRegistry INSTANCE = new GroupCallRegistry();

    static GroupCallRegistry get() {
        return INSTANCE;
    }

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Call> calls = new LinkedHashMap<>();
    private final List<Listener> listeners = new CopyOnWriteArrayList<>();

    private GroupCallRegistry() {}

    static String key(String acc, String room) {
        return acc + "\n" + room;
    }

    void addListener(Listener l) {
        listeners.add(l);
    }

    void removeListener(Listener l) {
        listeners.remove(l);
    }

    /** Main thread only. */
    Call find(String acc, String room) {
        return calls.get(key(acc, room));
    }

    /** Main thread only. */
    List<Call> current() {
        final List<Call> out = new ArrayList<>();
        for (Call c : calls.values()) {
            if (c.live()) {
                out.add(c);
            }
        }
        return out;
    }

    /** On tacky's thread: parsed here, applied on main. */
    void onState(byte[] frame) {
        final JSONObject a;
        try {
            a = new JSONArray(new String(frame, StandardCharsets.UTF_8)).optJSONObject(3);
        } catch (JSONException e) {
            Log.w(TAG, "unparseable groupcall state", e);
            return;
        }
        if (a != null) {
            main.post(() -> apply(a));
        }
    }

    private void apply(JSONObject a) {
        final String acc = a.optString("acc");
        final String room = a.optString("jid");
        if (acc.isEmpty() || room.isEmpty()) {
            return;
        }
        final String phase = a.optString("phase");
        Call c = calls.get(key(acc, room));
        if ("idle".equals(phase)) {
            if (c != null && !c.live()) {
                calls.remove(key(acc, room));
            }
            return;
        }
        if (c == null) {
            if ("ended".equals(phase)) {
                return; // a call we never showed
            }
            c = new Call(acc, room);
            calls.put(key(acc, room), c);
        }
        c.chat = a.optString("chat");
        c.hosted = a.optBoolean("hosted");
        c.phase = phase;
        c.reason = a.optString("reason");
        c.warning = a.optString("warning");
        c.since = a.optLong("since");
        c.video = a.optBoolean("video");
        c.camera = a.optBoolean("camera");
        final JSONObject preview = a.optJSONObject("preview");
        final String name = preview == null ? "" : preview.optString("name");
        c.preview = name.isEmpty() ? null : name;
        c.count = a.optInt("count");
        if (!"ended".equals(phase)) {
            final List<Participant> people = new ArrayList<>();
            final JSONArray list = a.optJSONArray("participants");
            for (int i = 0; list != null && i < list.length(); ++i) {
                final JSONObject o = list.optJSONObject(i);
                if (o == null) {
                    continue;
                }
                final Participant p = new Participant();
                p.nick = o.optString("nick");
                p.jid = o.optString("jid");
                p.key = p.jid.isEmpty() ? "/" + p.nick : p.jid;
                p.joined = o.optBoolean("joined");
                p.video = o.optBoolean("video");
                p.leg = o.optString("leg");
                p.reason = o.optString("reason");
                p.warning = o.optString("warning");
                p.stream = o.optString("stream");
                people.add(p);
            }
            c.participants = people;
        } else {
            // The tiles stay, greyed, until the screen closes.
            for (Participant p : c.participants) {
                if (!"failed".equals(p.leg)) {
                    p.leg = "ended";
                }
                p.stream = "";
                p.warning = "";
            }
            c.preview = null;
        }
        for (Listener l : listeners) {
            l.onGroupCallChanged(c);
        }
    }
}
