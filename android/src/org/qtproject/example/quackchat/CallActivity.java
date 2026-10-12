package org.qtproject.example.quackchat;

import android.Manifest;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Outline;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewOutlineProvider;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Chronometer;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import io.github.pounceandmiss.quack.R;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * The 1:1 call screen. It runs in the backend process, so it needs no Qt and
 * can show over the lock screen without exposing the rest of the app.
 */
public final class CallActivity extends Activity implements CallRegistry.Listener {
    private static final String TAG = "quack.callscreen";

    static final String EXTRA_SID = "sid";
    static final String ACTION_SHOW = "org.qtproject.example.quackchat.CALL_SHOW";
    static final String ACTION_ANSWER = "org.qtproject.example.quackchat.CALL_ANSWER";

    private static final int PERMISSIONS_FOR_ANSWER = 1;
    private static final long ENDED_LINGER_MS = 1500;
    private static final long FAILED_LINGER_MS = 4000;

    static Intent intent(Context ctx, CallRegistry.Call c, String action) {
        return new Intent(ctx, CallActivity.class)
                .setAction(action)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra(Notifications.EXTRA_ACC, c.acc)
                .putExtra(EXTRA_SID, c.sid);
    }

    static PendingIntent pending(Context ctx, CallRegistry.Call c, String action) {
        return PendingIntent.getActivity(ctx, (action + c.acc + c.sid).hashCode(),
                intent(ctx, c, action),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private final Handler main = new Handler(Looper.getMainLooper());
    private CallRegistry.Call call;

    private TextView status, name, jid, failure, warning, initial, account;
    private TextView videoName, videoStatus;
    private Chronometer duration, videoDuration;
    private ImageView avatar, verified, videoVerified;
    private View voiceContent, videoTop, controls, incomingRow, callRow;
    private View speakerControl, cameraControl, flipControl;
    private ImageButton mute, speaker, camera, flip;
    private TextView[] labels;
    private FrameStreamView remoteVideo, previewVideo;

    private static final long OVERLAY_HIDE_MS = 4000;
    private boolean overlaysShown = true;
    private final Runnable hideOverlays = () -> showOverlays(false);

    private double unmutedVolume = 1.0;
    private boolean muted;
    private boolean cameraOn = true;
    private PowerManager.WakeLock proximity;
    private boolean leaving;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setShowWhenLocked(true);
        setTurnScreenOn(true);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.call_activity);
        bindViews();
        CallRegistry.get().addListener(this);
        readMuted();
        take(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        take(intent);
    }

    @Override
    protected void onDestroy() {
        CallRegistry.get().removeListener(this);
        main.removeCallbacksAndMessages(null);
        holdProximity(false);
        super.onDestroy();
    }

    private void take(Intent intent) {
        final CallRegistry.Call c = CallRegistry.get().find(
                intent.getStringExtra(Notifications.EXTRA_ACC), intent.getStringExtra(EXTRA_SID));
        if (c == null) {
            // Over before the screen came up.
            finish();
            return;
        }
        call = c;
        leaving = false;
        render();
        if (ACTION_ANSWER.equals(intent.getAction()) && c.ringingHere()) {
            answer();
        }
    }

    // --- the user's choices ----------------------------------------------------

    private void answer() {
        final List<String> missing = new ArrayList<>();
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            missing.add(Manifest.permission.RECORD_AUDIO);
        }
        if (call.video && checkSelfPermission(Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {
            missing.add(Manifest.permission.CAMERA);
        }
        if (!missing.isEmpty()) {
            requestPermissions(missing.toArray(new String[0]), PERMISSIONS_FOR_ANSWER);
            return;
        }
        send("calls", "accept", args());
        // The service claims the microphone off this, which Android only
        // allows while we are visible.
        CallRegistry.get().accepted(call);
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        if (code != PERMISSIONS_FOR_ANSWER || call == null || !call.ringingHere()) {
            return;
        }
        for (int i = 0; i < perms.length; ++i) {
            if (Manifest.permission.RECORD_AUDIO.equals(perms[i])
                    && results[i] != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, R.string.call_needs_mic, Toast.LENGTH_LONG).show();
                return;
            }
        }
        send("calls", "accept", args());
        CallRegistry.get().accepted(call);
    }

    private void decline() {
        send("calls", "reject", args());
    }

    private void hangUp() {
        send("calls", call.ringingHere() ? "reject" : "hangup", args());
    }

    private void toggleMute() {
        muted = !muted;
        CallControls.setMuted(muted, unmutedVolume);
        render();
    }

    private void toggleSpeaker() {
        final CallAudio audio = QuackBackendService.audio();
        if (audio != null) {
            audio.setSpeaker(!audio.speakerOn());
        }
        render();
    }

    private void toggleCamera() {
        cameraOn = !cameraOn;
        final JSONObject a = args();
        put(a, "on", cameraOn);
        send("calls", "setVideo", a);
        render();
    }

    private void readMuted() {
        CallControls.readCaptureVolume(vol -> {
            muted = vol <= 0.0;
            if (!muted) {
                unmutedVolume = vol;
            }
            render();
        });
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        // Volume keys silence the ring.
        if (call != null && call.ringingHere()
                && (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)) {
            final CallNotifications notes = QuackBackendService.callNotifications();
            if (notes != null) {
                notes.silence(call);
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    // --- tacky ---------------------------------------------------------------------

    @Override
    public void onCallChanged(CallRegistry.Call c) {
        if (c != call) {
            return;
        }
        render();
        if (c.terminal() && !leaving) {
            leaving = true;
            main.postDelayed(this::finish,
                    "failed".equals(c.state) ? FAILED_LINGER_MS : ENDED_LINGER_MS);
        }
    }

    private JSONObject args() {
        return kv("acc", call.acc, "sid", call.sid);
    }

    private static JSONObject kv(Object... pairs) {
        final JSONObject o = new JSONObject();
        for (int i = 0; i + 1 < pairs.length; i += 2) {
            put(o, (String) pairs[i], pairs[i + 1]);
        }
        return o;
    }

    private static void put(JSONObject o, String k, Object v) {
        try {
            o.put(k, v);
        } catch (JSONException e) {
            throw new IllegalArgumentException(k, e);
        }
    }

    private void send(String module, String method, JSONObject a) {
        final FrameRouter router = QuackBackendService.router();
        if (router == null) {
            Log.w(TAG, "no backend for " + module + " " + method);
            return;
        }
        router.inject(module, method, a);
    }

    // --- drawing ------------------------------------------------------------------

    private void bindViews() {
        voiceContent = findViewById(R.id.voice_content);
        status = findViewById(R.id.status);
        name = findViewById(R.id.name);
        jid = findViewById(R.id.jid);
        failure = findViewById(R.id.failure);
        warning = findViewById(R.id.warning);
        duration = findViewById(R.id.duration);
        verified = findViewById(R.id.verified);
        avatar = findViewById(R.id.avatar);
        initial = findViewById(R.id.initial);
        account = findViewById(R.id.account);
        videoTop = findViewById(R.id.video_top);
        videoName = findViewById(R.id.video_name);
        videoStatus = findViewById(R.id.video_status);
        videoDuration = findViewById(R.id.video_duration);
        videoVerified = findViewById(R.id.video_verified);
        controls = findViewById(R.id.controls);
        incomingRow = findViewById(R.id.incoming_row);
        callRow = findViewById(R.id.call_row);
        mute = findViewById(R.id.mute);
        speaker = findViewById(R.id.speaker);
        camera = findViewById(R.id.camera);
        flip = findViewById(R.id.flip);
        speakerControl = findViewById(R.id.speaker_control);
        cameraControl = findViewById(R.id.camera_control);
        flipControl = findViewById(R.id.flip_control);
        labels = new TextView[] {
                findViewById(R.id.decline_label), findViewById(R.id.answer_label),
                findViewById(R.id.mute_label), findViewById(R.id.speaker_label),
                findViewById(R.id.camera_label), findViewById(R.id.flip_label),
                findViewById(R.id.hang_up_label)};
        remoteVideo = findViewById(R.id.remote_video);
        remoteVideo.setCrop(true);
        previewVideo = findViewById(R.id.preview_video);
        previewVideo.setZOrderMediaOverlay(true);
        previewVideo.setSelfView(true);

        avatar.setOutlineProvider(new ViewOutlineProvider() {
            @Override
            public void getOutline(View v, Outline o) {
                o.setOval(0, 0, v.getWidth(), v.getHeight());
            }
        });
        avatar.setClipToOutline(true);

        findViewById(R.id.answer).setOnClickListener(v -> answer());
        findViewById(R.id.decline).setOnClickListener(v -> decline());
        findViewById(R.id.hang_up).setOnClickListener(v -> hangUp());
        mute.setOnClickListener(v -> { toggleMute(); poke(); });
        speaker.setOnClickListener(v -> { toggleSpeaker(); poke(); });
        camera.setOnClickListener(v -> { toggleCamera(); poke(); });
        flip.setOnClickListener(v -> { CallControls.flipCamera(); poke(); });

        final View root = findViewById(R.id.root);
        root.setOnClickListener(v -> {
            if (videoLook()) {
                showOverlays(!overlaysShown);
            }
        });

        final int voiceTop = voiceContent.getPaddingTop();
        final int voiceBottom = voiceContent.getPaddingBottom();
        final int topPad = videoTop.getPaddingTop();
        final int controlsPad = controls.getPaddingBottom();
        final int warningMargin = ((FrameLayout.LayoutParams) warning.getLayoutParams()).topMargin;
        final int previewMargin = ((FrameLayout.LayoutParams) previewVideo.getLayoutParams()).topMargin;
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            final android.graphics.Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            voiceContent.setPadding(voiceContent.getPaddingLeft(), voiceTop + bars.top,
                    voiceContent.getPaddingRight(), voiceBottom + bars.bottom);
            videoTop.setPadding(videoTop.getPaddingLeft(), topPad + bars.top,
                    videoTop.getPaddingRight(), videoTop.getPaddingBottom());
            controls.setPadding(controls.getPaddingLeft(), controls.getPaddingTop(),
                    controls.getPaddingRight(), controlsPad + bars.bottom);
            setTopMargin(warning, warningMargin + bars.top);
            setTopMargin(previewVideo, previewMargin + bars.top);
            return insets;
        });
    }

    private static void setTopMargin(View v, int px) {
        final FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) v.getLayoutParams();
        lp.topMargin = px;
        v.setLayoutParams(lp);
    }

    private boolean videoLook() {
        return call != null && call.remoteStream != null && !call.terminal()
                && !call.ringingHere();
    }

    private void render() {
        final CallRegistry.Call c = call;
        if (c == null) {
            return;
        }
        final boolean video = videoLook();
        final String statusLine = statusText(c);
        final boolean showJid = !c.displayName().equals(c.peer);

        voiceContent.setVisibility(video ? View.GONE : View.VISIBLE);
        status.setText(statusLine);
        name.setText(c.displayName());
        jid.setText(c.peer);
        jid.setVisibility(showJid ? View.VISIBLE : View.GONE);
        account.setText(getString(R.string.call_using_account, c.acc));
        account.setVisibility(c.ringingHere() ? View.VISIBLE : View.GONE);
        failure.setText(c.reason);
        failure.setVisibility("failed".equals(c.state) && !c.reason.isEmpty()
                ? View.VISIBLE : View.GONE);
        verified.setVisibility(c.verified && !c.terminal() ? View.VISIBLE : View.GONE);

        final String letter = c.displayName().isEmpty() ? "?"
                : c.displayName().substring(0, c.displayName().offsetByCodePoints(0, 1))
                        .toUpperCase(java.util.Locale.ROOT);
        initial.setText(letter);
        avatar.setImageBitmap(c.avatar);
        avatar.setVisibility(c.avatar != null ? View.VISIBLE : View.GONE);

        videoTop.setVisibility(video && overlaysShown ? View.VISIBLE : View.GONE);
        videoName.setText(c.displayName());
        videoStatus.setText(statusLine);
        videoVerified.setVisibility(c.verified ? View.VISIBLE : View.GONE);
        controls.setBackgroundResource(video ? R.drawable.call_scrim_bottom : 0);
        controls.setVisibility(!video || overlaysShown ? View.VISIBLE : View.GONE);
        for (TextView label : labels) {
            label.setTextColor(video ? 0xffffffff : getColor(R.color.call_text_dim));
        }

        showDuration(duration, !video ? c : null);
        showDuration(videoDuration, video ? c : null);

        final boolean warn = !c.warning.isEmpty() && !c.terminal();
        warning.setText(c.warning);
        warning.setVisibility(warn ? View.VISIBLE : View.GONE);

        final boolean remote = c.remoteStream != null && !c.terminal();
        remoteVideo.setVisibility(remote ? View.VISIBLE : View.GONE);
        remoteVideo.show(remote ? c.remoteStream : null);
        final boolean preview = c.previewStream != null && cameraOn && !c.terminal();
        previewVideo.setVisibility(preview ? View.VISIBLE : View.GONE);
        previewVideo.show(preview ? c.previewStream : null);

        incomingRow.setVisibility(c.ringingHere() ? View.VISIBLE : View.GONE);
        callRow.setVisibility(!c.ringingHere() && !c.terminal() ? View.VISIBLE : View.GONE);

        final CallAudio audio = QuackBackendService.audio();
        final boolean speakerOn = audio != null && audio.speakerOn();
        toggle(mute, muted, muted ? R.drawable.ic_mic_off : R.drawable.ic_mic,
                muted ? R.string.call_unmute : R.string.call_mute, video);
        toggle(speaker, speakerOn, R.drawable.ic_speaker, R.string.call_speaker, video);
        speakerControl.setVisibility(c.video ? View.GONE : View.VISIBLE);
        cameraControl.setVisibility(c.video ? View.VISIBLE : View.GONE);
        flipControl.setVisibility(c.video && cameraOn ? View.VISIBLE : View.GONE);
        toggle(camera, !cameraOn, cameraOn ? R.drawable.ic_videocam : R.drawable.ic_videocam_off,
                cameraOn ? R.string.call_camera_off : R.string.call_camera_on, video);
        toggle(flip, false, R.drawable.ic_flip_camera, R.string.call_switch_camera, video);

        if (video && overlaysShown) {
            main.removeCallbacks(hideOverlays);
            main.postDelayed(hideOverlays, OVERLAY_HIDE_MS);
        } else if (!video) {
            main.removeCallbacks(hideOverlays);
            overlaysShown = true;
        }

        holdProximity("active".equals(c.state) && !c.video && audio != null && audio.atEar());
    }

    private void showDuration(Chronometer timer, CallRegistry.Call c) {
        if (c != null && c.since > 0 && !c.terminal()) {
            final long upMs = System.currentTimeMillis() - c.since / 1000;
            timer.setBase(SystemClock.elapsedRealtime() - upMs);
            timer.setVisibility(View.VISIBLE);
            timer.start();
        } else {
            timer.stop();
            timer.setVisibility(c != null && c.since > 0 ? View.VISIBLE : View.GONE);
        }
    }

    private void showOverlays(boolean show) {
        main.removeCallbacks(hideOverlays);
        if (!videoLook()) {
            overlaysShown = true;
            return;
        }
        overlaysShown = show;
        for (View v : new View[] {videoTop, controls}) {
            v.animate().cancel();
            if (show) {
                v.setAlpha(0f);
                v.setVisibility(View.VISIBLE);
                v.animate().alpha(1f).setDuration(150).start();
            } else {
                v.animate().alpha(0f).setDuration(250)
                        .withEndAction(() -> {
                            v.setVisibility(View.GONE);
                            v.setAlpha(1f);
                        }).start();
            }
        }
        if (show) {
            main.postDelayed(hideOverlays, OVERLAY_HIDE_MS);
        }
    }

    private void poke() {
        if (videoLook() && overlaysShown) {
            main.removeCallbacks(hideOverlays);
            main.postDelayed(hideOverlays, OVERLAY_HIDE_MS);
        }
    }

    private void toggle(ImageButton b, boolean on, int icon, int label, boolean video) {
        b.setImageResource(icon);
        b.setContentDescription(getString(label));
        final int fill;
        final int glyph;
        if (video) {
            fill = on ? 0xffffffff : 0x33ffffff;
            glyph = on ? 0xff202020 : 0xffffffff;
        } else {
            fill = getColor(on ? R.color.call_accent : R.color.call_surface);
            glyph = getColor(on ? R.color.call_on_accent : R.color.call_text);
        }
        b.setBackgroundTintList(ColorStateList.valueOf(fill));
        b.setImageTintList(ColorStateList.valueOf(glyph));
    }

    private String statusText(CallRegistry.Call c) {
        switch (c.state) {
            case "calling":    return getString(R.string.call_state_calling);
            case "ringing":    return getString(R.string.call_state_ringing);
            case "incoming":
                return getString(c.video ? R.string.call_incoming_video : R.string.call_incoming);
            case "connecting": return getString(R.string.call_state_connecting);
            case "active":     return getString(R.string.call_state_active);
            case "failed":     return getString(R.string.call_state_failed);
            default:           return getString(endText(c.reason));
        }
    }

    private static int endText(String reason) {
        switch (reason) {
            case "declined":  return R.string.call_end_declined;
            case "busy":      return R.string.call_end_busy;
            case "retracted": return R.string.call_end_retracted;
            case "elsewhere": return R.string.call_end_elsewhere;
            case "timeout":   return R.string.call_end_timeout;
            case "reset":     return R.string.call_end_reset;
            default:          return R.string.call_end_hangup;
        }
    }

    private void holdProximity(boolean hold) {
        if (hold && proximity == null) {
            final PowerManager pm = getSystemService(PowerManager.class);
            if (!pm.isWakeLockLevelSupported(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK)) {
                return;
            }
            proximity = pm.newWakeLock(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK,
                    "quack:call-proximity");
            proximity.acquire();
        } else if (!hold && proximity != null) {
            proximity.release(PowerManager.RELEASE_FLAG_WAIT_FOR_NO_PROXIMITY);
            proximity = null;
        }
    }
}
