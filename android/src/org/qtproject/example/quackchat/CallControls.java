package org.qtproject.example.quackchat;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** The call screens' controls. Mic and camera are machine-wide in tacky. */
final class CallControls {
    private static final String TAG = "quack.callcontrols";

    private CallControls() {}

    static JSONObject kv(Object... pairs) {
        final JSONObject o = new JSONObject();
        for (int i = 0; i + 1 < pairs.length; i += 2) {
            try {
                o.put((String) pairs[i], pairs[i + 1]);
            } catch (JSONException e) {
                throw new IllegalArgumentException((String) pairs[i], e);
            }
        }
        return o;
    }

    static void send(String module, String method, JSONObject args) {
        final FrameRouter router = QuackBackendService.router();
        if (router == null) {
            Log.w(TAG, "no backend for " + module + " " + method);
            return;
        }
        router.inject(module, method, args);
    }

    /** tacky's mute is a zero capture gain. */
    static void setMuted(boolean muted, double unmuted) {
        send("audio", "setVolume", kv("kind", "capture", "volume", muted ? 0.0 : unmuted));
    }

    interface VolumeCallback {
        void onVolume(double volume);
    }

    /** Answers on the main thread. */
    static void readCaptureVolume(VolumeCallback cb) {
        final FrameRouter router = QuackBackendService.router();
        if (router == null) {
            return;
        }
        final Handler main = new Handler(Looper.getMainLooper());
        router.request("audio", "getVolume", kv("kind", "capture"), (v, err) -> {
            if (v instanceof Number) {
                final double vol = ((Number) v).doubleValue();
                main.post(() -> cb.onVolume(vol));
            }
        });
    }

    /** The next camera facing the other way. */
    static void flipCamera() {
        final FrameRouter router = QuackBackendService.router();
        if (router == null) {
            return;
        }
        router.request("video", "enumerateCameras", new JSONObject(), (cams, err) ->
            router.request("video", "getPreferredCamera", new JSONObject(), (current, err2) -> {
                if (!(cams instanceof JSONArray) || ((JSONArray) cams).length() < 2) {
                    return;
                }
                final JSONArray list = (JSONArray) cams;
                final String now = current instanceof String ? (String) current : "";
                int at = 0;
                for (int i = 0; i < list.length(); ++i) {
                    if (now.equals(list.optJSONObject(i).optString("id"))) {
                        at = i;
                    }
                }
                final int facing = list.optJSONObject(at).optInt("facing", -1);
                for (int step = 1; step < list.length(); ++step) {
                    final JSONObject next = list.optJSONObject((at + step) % list.length());
                    if (next.optInt("facing", -2) != facing || step == list.length() - 1) {
                        send("video", "setPreferredCamera", kv("id", next.optString("id")));
                        return;
                    }
                }
            }));
    }
}
