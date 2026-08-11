package org.qtproject.example.quackchat;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * tacky's length-prefixed framing, as used on the UI socket. Translated from
 * lib/lenpipe/lenpipe.tcl in the tacky checkout; SocketTransport.cpp is the
 * third implementation of the same three lines of protocol.
 *
 * <pre>
 *   &lt;byte_length&gt;\n&lt;utf8_payload&gt;   repeating, no separator after the payload
 * </pre>
 *
 * The length counts bytes, not characters - getting that wrong desynchronises
 * the stream, and this framing has no resynchronisation point.
 */
final class Lenpipe {
    /**
     * Above any real payload (an unresized avatar as base64, a whole chatlist)
     * and low enough that a desynced length cannot be turned into an
     * allocation.
     */
    static final int MAX_FRAME = 64 * 1024 * 1024;

    private Lenpipe() {}

    static void write(OutputStream out, byte[] payload) throws IOException {
        out.write((Integer.toString(payload.length) + "\n").getBytes("UTF-8"));
        out.write(payload);
        out.flush();
    }

    /**
     * Reads one frame, or returns null at end of stream. Throws IOException on
     * a length that is unparseable or absurd: the caller drops the connection,
     * because there is nothing to resynchronise to.
     */
    static byte[] read(InputStream in) throws IOException {
        final ByteArrayOutputStream lenBytes = new ByteArrayOutputStream(12);
        int c;
        while ((c = in.read()) != -1 && c != '\n') {
            lenBytes.write(c);
            if (lenBytes.size() > 12) {
                throw new IOException("length prefix runaway");
            }
        }
        if (c == -1 && lenBytes.size() == 0) {
            return null; // clean end of stream between frames
        }
        final int len;
        try {
            len = Integer.parseInt(lenBytes.toString("UTF-8").trim());
        } catch (NumberFormatException e) {
            throw new IOException("bad length prefix", e);
        }
        if (len < 0 || len > MAX_FRAME) {
            throw new IOException("frame too large: " + len);
        }

        final byte[] payload = new byte[len];
        int off = 0;
        while (off < len) {
            final int n = in.read(payload, off, len - off);
            if (n < 0) {
                throw new IOException("truncated frame");
            }
            off += n;
        }
        return payload;
    }
}
