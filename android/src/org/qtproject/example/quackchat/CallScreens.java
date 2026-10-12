package org.qtproject.example.quackchat;

import android.app.Activity;
import android.app.KeyguardManager;
import android.app.PendingIntent;
import android.app.PictureInPictureParams;
import android.app.RemoteAction;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.util.Rational;

import io.github.pounceandmiss.quack.R;

import java.util.Collections;

/** What the 1:1 and group call screens share about stepping aside from a call. */
final class CallScreens {
    private CallScreens() {}

    /** The chat, in the app; over the lock screen, once it is unlocked. */
    static void openChat(Activity a, String acc, String jid, Runnable then) {
        final Runnable open = () -> {
            a.startActivity(Notifications.chatIntent(a, acc, jid));
            then.run();
        };
        final KeyguardManager km = a.getSystemService(KeyguardManager.class);
        if (km == null || !km.isKeyguardLocked()) {
            open.run();
            return;
        }
        km.requestDismissKeyguard(a, new KeyguardManager.KeyguardDismissCallback() {
            @Override
            public void onDismissSucceeded() {
                open.run();
            }
        });
    }

    /** The video call's floating window: entered on leaving, with hang-up as its action. */
    static void setPip(Activity a, boolean on, Rational aspect, PendingIntent end, int endLabel) {
        final PictureInPictureParams.Builder b = new PictureInPictureParams.Builder()
                .setAspectRatio(aspect)
                .setActions(Collections.singletonList(new RemoteAction(
                        Icon.createWithResource(a, R.drawable.ic_call_end),
                        a.getString(endLabel), a.getString(endLabel), end)));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            b.setAutoEnterEnabled(on).setSeamlessResizeEnabled(false);
        }
        a.setPictureInPictureParams(b.build());
    }

    /** Before Android 12 nothing enters on its own; returns whether it did. */
    static boolean enterPip(Activity a) {
        try {
            return a.enterPictureInPictureMode(new PictureInPictureParams.Builder().build());
        } catch (IllegalStateException e) {
            return false; // not allowed right now, e.g. over the lock screen
        }
    }
}
