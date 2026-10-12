package org.qtproject.example.quackchat;

import static org.robolectric.Shadows.shadowOf;

import android.os.Looper;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;

/** tacky's event frames, as the service hands them to the registries. */
final class Frames {
    private static final AtomicInteger next = new AtomicInteger();

    private Frames() {}

    /** A sid no other test has used: the registries are process-wide. */
    static String sid() {
        return "sid-" + next.incrementAndGet();
    }

    static JSONObject args(Object... pairs) {
        final JSONObject o = new JSONObject();
        try {
            for (int i = 0; i + 1 < pairs.length; i += 2) {
                o.put((String) pairs[i], pairs[i + 1]);
            }
        } catch (JSONException e) {
            throw new AssertionError(e);
        }
        return o;
    }

    /** Only index 3, the args, is read past the envelope. */
    static byte[] event(String module, String name, JSONObject args) {
        return new JSONArray().put(module).put("<" + name + ">").put(JSONObject.NULL).put(args)
                .toString().getBytes(StandardCharsets.UTF_8);
    }

    /** A calls event, applied: the registry posts to the main thread. */
    static void calls(String name, Object... pairs) {
        CallRegistry.get().onEvent(name, event("calls", name, args(pairs)));
        idle();
    }

    static void groupState(JSONObject state) {
        GroupCallRegistry.get().onState(event("groupcall", "State", state));
        idle();
    }

    static void idle() {
        shadowOf(Looper.getMainLooper()).idle();
    }
}
