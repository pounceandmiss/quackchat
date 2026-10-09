package org.qtproject.example.quackchat;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * Renames request tokens so tacky never sees one twice: its token map outlives
 * any UI process, and a new UI counts from 1 again. Replies owed to a UI that
 * has gone are dropped. Works on the bytes like {@link FrameRouter.Envelope},
 * and is plain Java so TokenMapCheck runs it without Android.
 */
final class TokenMap {
    /** The generation of an event, which goes to whoever is attached. */
    static final int ANY = -1;

    /** A frame on its way to the UI, and which connection it is for. */
    static final class Routed {
        final byte[] frame;
        final int gen;

        Routed(byte[] frame, int gen) {
            this.frame = frame;
            this.gen = gen;
        }
    }

    private static final class Entry {
        final int gen;
        final byte[] uiToken;

        Entry(int gen, byte[] uiToken) {
            this.gen = gen;
            this.uiToken = uiToken;
        }
    }

    private long next = 1;
    private final Map<Long, Entry> pending = new HashMap<>();

    /** UI to tacky: a request's trailing token becomes ours; notifies pass. */
    synchronized byte[] outbound(byte[] frame, int gen) {
        // Scan back from ']': digits after a ',' are the token.
        int i = frame.length - 1;
        i = skipSpaceBack(frame, i);
        if (i < 0 || frame[i] != ']') {
            return frame;
        }
        i = skipSpaceBack(frame, i - 1);
        final int digitsEnd = i + 1;
        while (i >= 0 && isDigit(frame[i])) {
            --i;
        }
        final int digitsStart = i + 1;
        if (digitsStart == digitsEnd) {
            return frame;
        }
        i = skipSpaceBack(frame, i);
        if (i < 0 || frame[i] != ',') {
            return frame;
        }
        final long ours = next++;
        pending.put(ours, new Entry(gen, slice(frame, digitsStart, digitsEnd)));
        return splice(frame, digitsStart, digitsEnd, ascii(ours));
    }

    /** tacky to UI: a reply gets the UI's token back; null if owed to nobody. */
    synchronized Routed inbound(byte[] frame) {
        int i = 0;
        while (i < frame.length && frame[i] != '[') {
            ++i;
        }
        i = skipSpace(frame, i + 1);
        if (i >= frame.length || frame[i] != '"') {
            return new Routed(frame, ANY);
        }
        final int kindStart = ++i;
        while (i < frame.length && frame[i] != '"') {
            ++i;
        }
        if (i >= frame.length) {
            return new Routed(frame, ANY);
        }
        final String kind = new String(frame, kindStart, i - kindStart, StandardCharsets.US_ASCII);
        if (!kind.equals("result") && !kind.equals("error")) {
            return new Routed(frame, ANY);
        }
        i = skipSpace(frame, i + 1);
        if (i >= frame.length || frame[i] != ',') {
            return null;
        }
        i = skipSpace(frame, i + 1);
        final int digitsStart = i;
        long token = 0;
        while (i < frame.length && isDigit(frame[i])) {
            token = token * 10 + (frame[i] - '0');
            ++i;
        }
        if (i == digitsStart) {
            return null; // not a token of ours: we only ever send numbers
        }
        final Entry e = pending.remove(token);
        if (e == null) {
            return null;
        }
        return new Routed(splice(frame, digitsStart, i, e.uiToken), e.gen);
    }

    /** Connection {@code gen} has gone: its late replies will be dropped. */
    synchronized void forget(int gen) {
        for (Iterator<Entry> it = pending.values().iterator(); it.hasNext(); ) {
            if (it.next().gen == gen) {
                it.remove();
            }
        }
    }

    /** Requests out and not yet answered, for the check. */
    synchronized int pendingCount() {
        return pending.size();
    }

    private static boolean isDigit(byte b) {
        return b >= '0' && b <= '9';
    }

    private static boolean isSpace(byte b) {
        return b == ' ' || b == '\n' || b == '\r' || b == '\t';
    }

    private static int skipSpace(byte[] f, int i) {
        while (i < f.length && isSpace(f[i])) {
            ++i;
        }
        return i;
    }

    private static int skipSpaceBack(byte[] f, int i) {
        while (i >= 0 && isSpace(f[i])) {
            --i;
        }
        return i;
    }

    private static byte[] ascii(long n) {
        return Long.toString(n).getBytes(StandardCharsets.US_ASCII);
    }

    private static byte[] slice(byte[] f, int from, int to) {
        final byte[] out = new byte[to - from];
        System.arraycopy(f, from, out, 0, out.length);
        return out;
    }

    /** {@code f} with {@code [from, to)} replaced by {@code with}. */
    private static byte[] splice(byte[] f, int from, int to, byte[] with) {
        final byte[] out = new byte[f.length - (to - from) + with.length];
        System.arraycopy(f, 0, out, 0, from);
        System.arraycopy(with, 0, out, from, with.length);
        System.arraycopy(f, to, out, from + with.length, f.length - to);
        return out;
    }
}
