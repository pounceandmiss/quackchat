package org.qtproject.example.quackchat;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

/**
 * The document behind a {@code content://} url. Its last path segment is a
 * provider row id, not a filename, so only the provider can say what the
 * document is called and what type it is.
 */
final class PickedDocument {
    private static final String TAG = "quack.picked";

    private PickedDocument() {}

    /** The document's own filename, or "" if the provider will not say. */
    static String displayName(Context context, String uri) {
        return query(context, uri, OpenableColumns.DISPLAY_NAME);
    }

    /** Its MIME type, or "" - still there for a name with no extension. */
    static String mimeType(Context context, String uri) {
        try {
            final String type = context.getContentResolver().getType(Uri.parse(uri));
            return type == null ? "" : type;
        } catch (Exception e) {
            Log.w(TAG, "getType(" + uri + ") failed", e);
            return "";
        }
    }

    private static String query(Context context, String uri, String column) {
        final ContentResolver resolver = context.getContentResolver();
        try (Cursor c = resolver.query(Uri.parse(uri), new String[] {column},
                                       null, null, null)) {
            if (c == null || !c.moveToFirst()) {
                return "";
            }
            final int i = c.getColumnIndex(column);
            if (i < 0 || c.isNull(i)) {
                return "";
            }
            final String value = c.getString(i);
            return value == null ? "" : value;
        } catch (Exception e) {
            // A provider that refuses the query, or a url whose permission
            // lapsed: the caller falls back on the url itself.
            Log.w(TAG, "query(" + uri + ", " + column + ") failed", e);
            return "";
        }
    }
}
