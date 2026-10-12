package org.qtproject.example.quackchat;

import android.app.NotificationManager;
import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;

import java.util.HashMap;
import java.util.Map;

/**
 * The incoming ring: default ringtone and vibration, looped while any ring is
 * wanted. Played here, not as the channel's sound, so it stops exactly when the
 * call is answered, declined or silenced. Ringer mode and DND are checked by hand.
 */
final class CallRinger {
    private static final String TAG = "quack.ringer";

    /** Conversations' pattern. */
    private static final long[] VIBRATION = {0, 500, 300, 600, 3000};

    private static final AudioAttributes RING = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_NOTIFICATION_RINGTONE)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build();

    private final Context ctx;
    private final AudioManager am;
    private final NotificationManager nm;
    private final Vibrator vibrator;
    private final Handler main = new Handler(Looper.getMainLooper());
    /** Wanted rings, each with its timeout or null. */
    private final Map<String, Runnable> wanted = new HashMap<>();
    private Ringtone ringtone;
    private AudioFocusRequest focus;
    private boolean vibrating;

    CallRinger(Context ctx) {
        this.ctx = ctx;
        am = ctx.getSystemService(AudioManager.class);
        nm = ctx.getSystemService(NotificationManager.class);
        vibrator = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                ? ctx.getSystemService(VibratorManager.class).getDefaultVibrator()
                : ctx.getSystemService(Vibrator.class);
    }

    /** Any thread. Rings until stop(key), or for timeoutMs when positive. */
    void ring(String key, long timeoutMs) {
        main.post(() -> {
            if (wanted.containsKey(key)) {
                return;
            }
            final Runnable timeout = timeoutMs > 0 ? () -> stopNow(key) : null;
            wanted.put(key, timeout);
            if (timeout != null) {
                main.postDelayed(timeout, timeoutMs);
            }
            if (wanted.size() == 1) {
                start();
            }
        });
    }

    /** Any thread. */
    void stop(String key) {
        main.post(() -> stopNow(key));
    }

    /** Any thread. */
    void stopAll() {
        main.post(() -> {
            for (Runnable timeout : wanted.values()) {
                if (timeout != null) {
                    main.removeCallbacks(timeout);
                }
            }
            wanted.clear();
            halt();
        });
    }

    private void stopNow(String key) {
        if (!wanted.containsKey(key)) {
            return;
        }
        final Runnable timeout = wanted.remove(key);
        if (timeout != null) {
            main.removeCallbacks(timeout);
        }
        if (wanted.isEmpty()) {
            halt();
        }
    }

    private void start() {
        if (!dndAllowsCalls()) {
            Log.i(TAG, "Do Not Disturb: not ringing");
            return;
        }
        final int mode = am.getRingerMode();
        if (mode == AudioManager.RINGER_MODE_SILENT) {
            return;
        }
        if (vibrator != null && vibrator.hasVibrator()) {
            vibrator.vibrate(VibrationEffect.createWaveform(VIBRATION, 0), RING);
            vibrating = true;
        }
        if (mode != AudioManager.RINGER_MODE_NORMAL) {
            return;
        }
        final Uri uri = RingtoneManager.getActualDefaultRingtoneUri(ctx, RingtoneManager.TYPE_RINGTONE);
        ringtone = uri == null ? null : RingtoneManager.getRingtone(ctx, uri);
        if (ringtone == null) {
            Log.w(TAG, "no default ringtone");
            return;
        }
        ringtone.setAudioAttributes(RING);
        ringtone.setLooping(true);
        focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                .setAudioAttributes(RING)
                .build();
        am.requestAudioFocus(focus);
        ringtone.play();
    }

    private void halt() {
        if (ringtone != null) {
            ringtone.stop();
            ringtone = null;
        }
        if (focus != null) {
            am.abandonAudioFocusRequest(focus);
            focus = null;
        }
        if (vibrating) {
            vibrator.cancel();
            vibrating = false;
        }
    }

    /** Priority mode lets calls through only when it admits calls from anyone. */
    private boolean dndAllowsCalls() {
        switch (nm.getCurrentInterruptionFilter()) {
            case NotificationManager.INTERRUPTION_FILTER_ALL:
            case NotificationManager.INTERRUPTION_FILTER_UNKNOWN:
                return true;
            case NotificationManager.INTERRUPTION_FILTER_PRIORITY:
                final NotificationManager.Policy p = nm.getNotificationPolicy();
                return (p.priorityCategories & NotificationManager.Policy.PRIORITY_CATEGORY_CALLS) != 0
                        && p.priorityCallSenders == NotificationManager.Policy.PRIORITY_SENDERS_ANY;
            default:
                return false;
        }
    }
}
