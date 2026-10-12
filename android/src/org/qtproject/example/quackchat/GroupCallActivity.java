package org.qtproject.example.quackchat;

import android.Manifest;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Rational;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import io.github.pounceandmiss.quack.R;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The group call screen, in the backend process like {@link CallActivity}.
 * Opened by the service when a join starts, or by a ringing invite, which
 * shows Decline / Join until answered.
 */
public final class GroupCallActivity extends Activity implements GroupCallRegistry.Listener {
    static final String ACTION_SHOW = "org.qtproject.example.quackchat.GROUP_SHOW";
    static final String ACTION_ANSWER = "org.qtproject.example.quackchat.GROUP_ANSWER";
    static final String EXTRA_ROOM = "room";
    static final String EXTRA_CHAT = "chat";
    static final String EXTRA_TS = "ts";
    static final String EXTRA_FROM = "from";
    static final String EXTRA_VIDEO = "video";

    private static final int PERMISSIONS_FOR_JOIN = 1;
    private static final long ENDED_LINGER_MS = 1500;
    private static final long FAILED_LINGER_MS = 4000;

    static final class Invite {
        final String acc;
        final String room;
        final String chat;
        final long timestamp;
        final String from;
        final boolean video;

        Invite(String acc, String room, String chat, long timestamp, String from,
               boolean video) {
            this.acc = acc;
            this.room = room;
            this.chat = chat;
            this.timestamp = timestamp;
            this.from = from;
            this.video = video;
        }
    }

    static Intent intent(Context ctx, String acc, String room) {
        return new Intent(ctx, GroupCallActivity.class)
                .setAction(ACTION_SHOW)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra(Notifications.EXTRA_ACC, acc)
                .putExtra(EXTRA_ROOM, room);
    }

    static PendingIntent pending(Context ctx, String acc, String room) {
        return PendingIntent.getActivity(ctx, (ACTION_SHOW + acc + room).hashCode(),
                intent(ctx, acc, room),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    static PendingIntent pendingInvite(Context ctx, Invite inv, String action) {
        final Intent i = intent(ctx, inv.acc, inv.room)
                .setAction(action)
                .putExtra(EXTRA_CHAT, inv.chat)
                .putExtra(EXTRA_TS, inv.timestamp)
                .putExtra(EXTRA_FROM, inv.from)
                .putExtra(EXTRA_VIDEO, inv.video);
        return PendingIntent.getActivity(ctx, (action + inv.acc + inv.room).hashCode(), i,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    /** Called from the Qt UI over JNI (the chat banner's Open). */
    public static void show(Context ctx, String acc, String room) {
        ctx.startActivity(intent(ctx, acc, room));
    }

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Runnable tick = this::updateStatus;
    private String acc;
    private String room;
    /** Set while an invite rings here. */
    private Invite invite;
    private boolean leaving;

    private static final Rational PIP_ASPECT = new Rational(3, 4);
    private boolean inPip;
    private Boolean pipOn;
    /** Out of the floating window, either back to full screen or closed. */
    private boolean leftPip;
    private boolean stopped;

    private TextView title, status, failure, warning;
    private View inviteRow, callRow, cameraControl, flipControl;
    private ImageButton mute, speaker, camera, flip, toChat;
    private View speakerControl;
    private TextView muteLabel;
    private TileGrid grid;
    private final Map<String, Tile> tiles = new LinkedHashMap<>();
    private Tile selfTile;

    private double unmutedVolume = 1.0;
    private boolean muted;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setShowWhenLocked(true);
        setTurnScreenOn(true);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        inPip = isInPictureInPictureMode(); // recreated while floating
        setContentView(R.layout.group_call_activity);
        bindViews();
        GroupCallRegistry.get().addListener(this);
        CallControls.readCaptureVolume(vol -> {
            muted = vol <= 0.0;
            if (!muted) {
                unmutedVolume = vol;
            }
            render();
        });
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
        GroupCallRegistry.get().removeListener(this);
        main.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    @Override
    protected void onStart() {
        super.onStart();
        stopped = false;
    }

    @Override
    protected void onResume() {
        super.onResume();
        leftPip = false;
    }

    // The floating window swiped away ends the screen, not the call. Android
    // reports that as leaving picture-in-picture while already stopped, or
    // (older releases) the other way round; expanding it resumes in between.
    @Override
    protected void onStop() {
        super.onStop();
        stopped = true;
        if (leftPip) {
            finish();
        }
    }

    @Override
    public void onPictureInPictureModeChanged(boolean pip, Configuration config) {
        super.onPictureInPictureModeChanged(pip, config);
        if (!pip && stopped) {
            finish();
            return;
        }
        inPip = pip;
        leftPip = !pip;
        render();
    }

    @Override
    protected void onUserLeaveHint() {
        super.onUserLeaveHint();
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S && pipWanted()) {
            CallScreens.enterPip(this);
        }
    }

    @Override
    public void onBackPressed() {
        final GroupCallRegistry.Call c = live();
        if (c != null) {
            backToChat(c);
            return;
        }
        super.onBackPressed();
    }

    /** The call we are in, not one only ringing here. */
    private GroupCallRegistry.Call live() {
        final GroupCallRegistry.Call c = GroupCallRegistry.get().find(acc, room);
        return invite == null && c != null && c.live() ? c : null;
    }

    private boolean pipWanted() {
        final GroupCallRegistry.Call c = live();
        return c != null && c.video;
    }

    /** A video call keeps floating over the chat; a voice call needs no screen. */
    private void backToChat(GroupCallRegistry.Call c) {
        final boolean floating = pipWanted() && CallScreens.enterPip(this);
        CallScreens.openChat(this, acc, c.chat.isEmpty() ? room : c.chat,
                floating ? () -> {} : this::finish);
    }

    private void take(Intent intent) {
        acc = intent.getStringExtra(Notifications.EXTRA_ACC);
        room = intent.getStringExtra(EXTRA_ROOM);
        leaving = false;
        pipOn = null;
        invite = null;
        if (intent.hasExtra(EXTRA_CHAT) && GroupCallRegistry.get().find(acc, room) == null) {
            invite = new Invite(acc, room, intent.getStringExtra(EXTRA_CHAT),
                    intent.getLongExtra(EXTRA_TS, 0), intent.getStringExtra(EXTRA_FROM),
                    intent.getBooleanExtra(EXTRA_VIDEO, false));
        }
        if (invite == null && GroupCallRegistry.get().find(acc, room) == null) {
            finish(); // over before the screen came up
            return;
        }
        render();
        if (ACTION_ANSWER.equals(intent.getAction()) && invite != null) {
            join();
        }
    }

    // --- the user's choices ----------------------------------------------------

    private void join() {
        final List<String> missing = new ArrayList<>();
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            missing.add(Manifest.permission.RECORD_AUDIO);
        }
        if (invite.video && checkSelfPermission(Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {
            missing.add(Manifest.permission.CAMERA);
        }
        if (!missing.isEmpty()) {
            requestPermissions(missing.toArray(new String[0]), PERMISSIONS_FOR_JOIN);
            return;
        }
        sendJoin();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        if (code != PERMISSIONS_FOR_JOIN || invite == null) {
            return;
        }
        for (int i = 0; i < perms.length; ++i) {
            if (Manifest.permission.RECORD_AUDIO.equals(perms[i])
                    && results[i] != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, R.string.call_needs_mic, Toast.LENGTH_LONG).show();
                return;
            }
        }
        sendJoin();
    }

    private void sendJoin() {
        final Invite inv = invite;
        CallControls.send("groupcall", "join", CallControls.kv("acc", inv.acc,
                "chat", inv.chat, "timestamp", inv.timestamp, "video", inv.video ? 1 : 0));
        final CallNotifications notes = QuackBackendService.callNotifications();
        if (notes != null) {
            notes.cancelGroupInvite(inv.acc, inv.room);
        }
        invite = null;
        render();
    }

    private void decline() {
        final Invite inv = invite;
        if (inv == null) {
            return;
        }
        CallControls.send("groupcall", "decline", CallControls.kv("acc", inv.acc,
                "chat", inv.chat, "timestamp", inv.timestamp));
        final CallNotifications notes = QuackBackendService.callNotifications();
        if (notes != null) {
            notes.cancelGroupInvite(inv.acc, inv.room);
        }
        finish();
    }

    private void leave() {
        CallControls.send("groupcall", "leave", CallControls.kv("acc", acc, "jid", room));
    }

    // --- tacky -----------------------------------------------------------------

    @Override
    public void onGroupCallChanged(GroupCallRegistry.Call c) {
        if (!c.acc.equals(acc) || !c.room.equals(room)) {
            return;
        }
        if (c.live()) {
            invite = null; // answered, here or elsewhere
        }
        render();
        if ("ended".equals(c.phase) && !leaving) {
            leaving = true;
            main.postDelayed(this::finish,
                    c.reason.isEmpty() ? ENDED_LINGER_MS : FAILED_LINGER_MS);
        }
    }

    // --- drawing -----------------------------------------------------------------

    private void bindViews() {
        title = findViewById(R.id.title);
        status = findViewById(R.id.status);
        failure = findViewById(R.id.failure);
        warning = findViewById(R.id.warning);
        inviteRow = findViewById(R.id.invite_row);
        callRow = findViewById(R.id.call_row);
        cameraControl = findViewById(R.id.camera_control);
        flipControl = findViewById(R.id.flip_control);
        mute = findViewById(R.id.mute);
        muteLabel = findViewById(R.id.mute_label);
        speaker = findViewById(R.id.speaker);
        camera = findViewById(R.id.camera);
        flip = findViewById(R.id.flip);
        toChat = findViewById(R.id.to_chat);
        speakerControl = findViewById(R.id.speaker_control);
        grid = new TileGrid(this);
        ((FrameLayout) findViewById(R.id.grid_frame)).addView(grid,
                new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT));

        findViewById(R.id.join).setOnClickListener(v -> join());
        findViewById(R.id.decline).setOnClickListener(v -> decline());
        findViewById(R.id.leave).setOnClickListener(v -> leave());
        mute.setOnClickListener(v -> {
            muted = !muted;
            CallControls.setMuted(muted, unmutedVolume);
            render();
        });
        speaker.setOnClickListener(v -> {
            final CallAudio audio = QuackBackendService.audio();
            if (audio != null) {
                audio.setSpeaker(!audio.speakerOn());
            }
            render();
        });
        camera.setOnClickListener(v -> {
            final GroupCallRegistry.Call c = GroupCallRegistry.get().find(acc, room);
            if (c != null) {
                CallControls.send("groupcall", "setVideo",
                        CallControls.kv("acc", acc, "jid", room, "on", !c.camera ? 1 : 0));
            }
        });
        flip.setOnClickListener(v -> CallControls.flipCamera());
        toChat.setOnClickListener(v -> {
            final GroupCallRegistry.Call c = live();
            if (c != null) {
                backToChat(c);
            }
        });

        final View content = findViewById(R.id.content);
        final int padTop = content.getPaddingTop();
        final int padBottom = content.getPaddingBottom();
        final int warnTop = ((FrameLayout.LayoutParams) warning.getLayoutParams()).topMargin;
        final int toChatTop = ((FrameLayout.LayoutParams) toChat.getLayoutParams()).topMargin;
        findViewById(R.id.root).setOnApplyWindowInsetsListener((v, insets) -> {
            final android.graphics.Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            content.setPadding(content.getPaddingLeft(), padTop + bars.top,
                    content.getPaddingRight(), padBottom + bars.bottom);
            final FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) warning.getLayoutParams();
            lp.topMargin = warnTop + bars.top;
            warning.setLayoutParams(lp);
            final FrameLayout.LayoutParams back = (FrameLayout.LayoutParams) toChat.getLayoutParams();
            back.topMargin = toChatTop + bars.top;
            toChat.setLayoutParams(back);
            return insets;
        });
    }

    private void render() {
        final GroupCallRegistry.Call c = GroupCallRegistry.get().find(acc, room);
        updatePip();
        if (invite != null && (c == null || !c.live())) {
            renderInvite();
            return;
        }
        inviteRow.setVisibility(View.GONE);
        if (c == null) {
            return;
        }
        title.setText(c.title());
        updateStatus();
        failure.setText(c.reason);
        failure.setVisibility("ended".equals(c.phase) && !c.reason.isEmpty()
                ? View.VISIBLE : View.GONE);
        warning.setText(c.warning);
        warning.setVisibility(c.warning.isEmpty() || !c.live() || inPip ? View.GONE : View.VISIBLE);
        // The floating window is the grid alone.
        title.setVisibility(inPip ? View.GONE : View.VISIBLE);
        status.setVisibility(inPip ? View.GONE : View.VISIBLE);
        callRow.setVisibility(inPip ? View.GONE : c.live() ? View.VISIBLE : View.INVISIBLE);
        toChat.setVisibility(c.live() && !inPip ? View.VISIBLE : View.GONE);
        // Video is on the speaker anyway; hiding it keeps the row on a phone.
        speakerControl.setVisibility(c.video ? View.GONE : View.VISIBLE);
        cameraControl.setVisibility(c.video ? View.VISIBLE : View.GONE);
        flipControl.setVisibility(c.video && c.camera ? View.VISIBLE : View.GONE);

        final CallAudio audio = QuackBackendService.audio();
        toggle(mute, muted, muted ? R.drawable.ic_mic_off : R.drawable.ic_mic);
        muteLabel.setText(muted ? R.string.call_unmute : R.string.call_mute);
        toggle(speaker, audio != null && audio.speakerOn(), R.drawable.ic_speaker);
        toggle(camera, !c.camera, c.camera ? R.drawable.ic_videocam : R.drawable.ic_videocam_off);
        toggle(flip, false, R.drawable.ic_flip_camera);

        final Map<String, Tile> keep = new HashMap<>();
        final List<View> order = new ArrayList<>();
        for (GroupCallRegistry.Participant p : c.participants) {
            Tile t = tiles.get(p.key);
            if (t == null) {
                t = new Tile(this, false);
            }
            t.show(p.nick, legText(p), p.stream.isEmpty() ? null : p.stream);
            keep.put(p.key, t);
            order.add(t.view);
        }
        for (Map.Entry<String, Tile> e : tiles.entrySet()) {
            if (!keep.containsKey(e.getKey())) {
                e.getValue().video.show(null);
            }
        }
        tiles.clear();
        tiles.putAll(keep);
        if (selfTile == null) {
            selfTile = new Tile(this, true);
        }
        selfTile.show(getString(R.string.group_call_you), "",
                c.camera && c.live() ? c.preview : null);
        order.add(selfTile.view);
        grid.setTiles(order);
    }

    private void renderInvite() {
        title.setText(invite.chat);
        status.setText(getString(invite.video ? R.string.group_call_invite_video
                                              : R.string.group_call_invite, invite.from));
        failure.setVisibility(View.GONE);
        warning.setVisibility(View.GONE);
        inviteRow.setVisibility(View.VISIBLE);
        callRow.setVisibility(View.GONE);
        toChat.setVisibility(View.GONE);
        grid.setTiles(new ArrayList<>());
    }

    private void updatePip() {
        final boolean pip = pipWanted();
        if (pipOn == null || pipOn != pip) {
            pipOn = pip;
            CallScreens.setPip(this, pip, PIP_ASPECT,
                    CallNotifications.leavePending(this, acc, room), R.string.group_call_leave);
        }
    }

    /** The clock's tick redraws only this. */
    private void updateStatus() {
        main.removeCallbacks(tick);
        final GroupCallRegistry.Call c = GroupCallRegistry.get().find(acc, room);
        if (c == null || invite != null && !c.live()) {
            return;
        }
        status.setText(statusText(c));
        if ("live".equals(c.phase) && c.since > 0 && !c.participants.isEmpty()) {
            main.postDelayed(tick, 1000);
        }
    }

    private String statusText(GroupCallRegistry.Call c) {
        switch (c.phase) {
            case "joining":
                return getString(R.string.group_call_joining);
            case "ended":
                return getString(R.string.group_call_ended);
            default:
                break;
        }
        if (c.participants.isEmpty()) {
            return getString(R.string.group_call_waiting);
        }
        String text = getString(R.string.group_call_count, c.count);
        if (c.since > 0) {
            final long secs = Math.max(0, (System.currentTimeMillis() - c.since / 1000) / 1000);
            text += "  ·  " + (secs >= 3600
                    ? String.format(Locale.ROOT, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60)
                    : String.format(Locale.ROOT, "%d:%02d", secs / 60, secs % 60));
        }
        return text;
    }

    private String legText(GroupCallRegistry.Participant p) {
        switch (p.leg) {
            case "expected":
                return getString(p.joined ? R.string.group_call_leg_expected
                                          : R.string.group_call_leg_joining);
            case "connecting":
                return getString(R.string.group_call_leg_connecting);
            case "active":
                return p.warning;
            case "ended":
                return p.reason.isEmpty() ? getString(R.string.group_call_leg_ended) : p.reason;
            case "failed":
                return p.reason.isEmpty() ? getString(R.string.group_call_leg_failed) : p.reason;
            default:
                return "";
        }
    }

    private void toggle(ImageButton b, boolean on, int icon) {
        b.setImageResource(icon);
        b.setBackgroundTintList(ColorStateList.valueOf(getColor(
                on ? R.color.call_accent : R.color.call_surface)));
        b.setImageTintList(ColorStateList.valueOf(getColor(
                on ? R.color.call_on_accent : R.color.call_text)));
    }

    private static final class Tile {
        final FrameLayout view;
        final FrameStreamView video;
        final TextView initial;
        final TextView name;
        final TextView caption;
        private String stream;

        Tile(Context ctx, boolean self) {
            final float dp = ctx.getResources().getDisplayMetrics().density;
            view = new FrameLayout(ctx);
            view.setBackgroundResource(R.drawable.call_tile);
            view.setClipToOutline(true);

            initial = new TextView(ctx);
            initial.setBackgroundResource(R.drawable.call_button);
            initial.setBackgroundTintList(ColorStateList.valueOf(ctx.getColor(R.color.call_accent)));
            initial.setTextColor(ctx.getColor(R.color.call_on_accent));
            initial.setTextSize(32);
            initial.setGravity(Gravity.CENTER);
            view.addView(initial, new FrameLayout.LayoutParams((int) (72 * dp), (int) (72 * dp),
                    Gravity.CENTER));

            video = new FrameStreamView(ctx);
            video.setTile(12 * dp);
            if (self) {
                video.setSelfView(true);
            }
            video.setZOrderMediaOverlay(true);
            video.setVisibility(View.GONE);
            view.addView(video, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

            final LinearLayout label = new LinearLayout(ctx);
            label.setOrientation(LinearLayout.VERTICAL);
            label.setBackgroundResource(R.drawable.call_tile_scrim);
            label.setPadding((int) (10 * dp), (int) (16 * dp), (int) (10 * dp), (int) (8 * dp));
            name = new TextView(ctx);
            name.setTextColor(0xffffffff);
            name.setTextSize(14);
            name.setSingleLine(true);
            caption = new TextView(ctx);
            caption.setTextColor(0xd9ffffff);
            caption.setTextSize(12);
            caption.setSingleLine(true);
            label.addView(name);
            label.addView(caption);
            view.addView(label, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.BOTTOM));
        }

        void show(String nick, String text, String streamName) {
            // setText re-lays out even for the same text.
            if (!nick.contentEquals(name.getText())) {
                name.setText(nick);
                initial.setText(nick.isEmpty() ? "?"
                        : nick.substring(0, nick.offsetByCodePoints(0, 1)).toUpperCase(Locale.ROOT));
            }
            if (!text.contentEquals(caption.getText())) {
                caption.setText(text);
                caption.setVisibility(text.isEmpty() ? View.GONE : View.VISIBLE);
            }
            if (streamName == null ? stream != null : !streamName.equals(stream)) {
                stream = streamName;
                video.show(streamName);
                video.setVisibility(streamName == null ? View.GONE : View.VISIBLE);
            }
        }
    }

    private static final class TileGrid extends ViewGroup {
        private final int gap;

        TileGrid(Context ctx) {
            super(ctx);
            gap = (int) (8 * ctx.getResources().getDisplayMetrics().density);
        }

        /** Adds and removes only what changed: re-adding a tile flickers its video. */
        void setTiles(List<View> views) {
            for (int i = getChildCount() - 1; i >= 0; --i) {
                if (!views.contains(getChildAt(i))) {
                    removeViewAt(i);
                }
            }
            for (int i = 0; i < views.size(); ++i) {
                final View v = views.get(i);
                if (i < getChildCount() && getChildAt(i) == v) {
                    continue;
                }
                if (v.getParent() == this) {
                    removeView(v);
                } else if (v.getParent() instanceof ViewGroup) {
                    ((ViewGroup) v.getParent()).removeView(v);
                }
                addView(v, Math.min(i, getChildCount()));
            }
        }

        private int cols() {
            final int n = getChildCount();
            return n <= 2 ? 1 : n <= 4 ? 2 : 3;
        }

        @Override
        protected void onMeasure(int widthSpec, int heightSpec) {
            final int w = MeasureSpec.getSize(widthSpec);
            final int h = MeasureSpec.getSize(heightSpec);
            setMeasuredDimension(w, h);
            final int n = getChildCount();
            if (n == 0) {
                return;
            }
            final int cols = cols();
            final int rows = (n + cols - 1) / cols;
            final int cw = (w - gap * (cols - 1)) / cols;
            final int ch = (h - gap * (rows - 1)) / rows;
            for (int i = 0; i < n; ++i) {
                getChildAt(i).measure(MeasureSpec.makeMeasureSpec(cw, MeasureSpec.EXACTLY),
                        MeasureSpec.makeMeasureSpec(ch, MeasureSpec.EXACTLY));
            }
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            final int n = getChildCount();
            if (n == 0) {
                return;
            }
            final int cols = cols();
            for (int i = 0; i < n; ++i) {
                final View v = getChildAt(i);
                final int col = i % cols;
                final int row = i / cols;
                final int x = col * (v.getMeasuredWidth() + gap);
                final int y = row * (v.getMeasuredHeight() + gap);
                v.layout(x, y, x + v.getMeasuredWidth(), y + v.getMeasuredHeight());
            }
        }
    }
}
