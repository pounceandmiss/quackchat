package org.qtproject.example.quackchat;

import android.app.Activity;
import android.content.Context;
import android.view.View;
import android.view.WindowInsets;

/**
 * Reports the on-screen keyboard's height to backend/Keyboard.cpp, from the
 * keyboard's window insets.
 *
 * On the content view rather than the decor view: Qt sets and clears its own
 * callbacks on the decor view whenever it shows the keyboard.
 */
final class KeyboardInsets {
    private KeyboardInsets() {}

    /** In the scene's units, from the window's bottom edge; 0 when down. */
    private static native void heightChanged(float height);

    static void install(Context context) {
        if (!(context instanceof Activity))
            return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            final View content = activity.findViewById(android.R.id.content);
            if (content == null)
                return;
            final float density = activity.getResources().getDisplayMetrics().density;
            content.setOnApplyWindowInsetsListener((view, insets) -> {
                heightChanged(insets.getInsets(WindowInsets.Type.ime()).bottom / density);
                return view.onApplyWindowInsets(insets);
            });
            content.requestApplyInsets();
        });
    }
}
