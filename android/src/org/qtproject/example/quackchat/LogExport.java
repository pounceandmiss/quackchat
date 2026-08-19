package org.qtproject.example.quackchat;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;

/**
 * Handing the debug log to whatever the user wants to send it with. The file
 * lives in the app's own storage, where nothing else can read it, so it goes
 * out as a {@code content://} url from our provider with read permission
 * attached to the intent.
 */
final class LogExport {
    private static final String TAG = "quack.logexport";

    private LogExport() {}

    /** Offer the file at {@code path} to the share chooser. */
    static void share(Context context, String path, String title) {
        final File file = new File(path);
        if (!file.isFile()) {
            Log.w(TAG, "no log file at " + path);
            return;
        }
        try {
            final Uri uri = FileProvider.getUriForFile(
                    context, context.getPackageName() + ".qtprovider", file);
            // text/plain: a log is text, and the chooser offers mail and chat
            // apps for it rather than the handful that claim */*.
            final Intent send = new Intent(Intent.ACTION_SEND)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_STREAM, uri)
                    .putExtra(Intent.EXTRA_SUBJECT, title)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            // The chooser is its own task: started from the activity's context
            // it would otherwise land behind us.
            final Intent chooser = Intent.createChooser(send, title)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(chooser);
        } catch (Exception e) {
            Log.w(TAG, "could not share " + path, e);
        }
    }
}
