package org.qtproject.example.quackchat;

import java.nio.charset.StandardCharsets;

/** TokenMap's checks, run by android/in-image.sh before gradle. No JUnit here. */
public final class TokenMapCheck {
    private static int failures = 0;

    public static void main(String[] args) {
        renamesThereAndBack();
        twoConnectionsSendingTheSameTokenStayApart();
        aGoneConnectionsRepliesGoNowhere();
        whatCarriesNoTokenPassesThrough();
        tokensAreNeverHandedOutTwice();
        if (failures > 0) {
            System.err.println("TokenMapCheck: " + failures + " failed");
            System.exit(1);
        }
        System.out.println("TokenMapCheck: ok");
    }

    private static void renamesThereAndBack() {
        final TokenMap m = new TokenMap();
        final byte[] sent = m.outbound(b("[\"message\",\"history\",{\"chat\":\"a]1,2\\\"\"},7]"), 1);
        final long ours = trailingToken(sent);
        check(s(sent).equals("[\"message\",\"history\",{\"chat\":\"a]1,2\\\"\"}," + ours + "]"),
              "the request kept its payload: " + s(sent));

        final TokenMap.Routed back = m.inbound(b("[\"result\"," + ours + ",[{\"body\":\"12 [3]\"}]]"));
        check(back != null && back.gen == 1, "the reply goes to connection 1");
        check(back != null && s(back.frame).equals("[\"result\",7,[{\"body\":\"12 [3]\"}]]"),
              "the reply carries the UI's token: " + (back == null ? null : s(back.frame)));
        check(m.pendingCount() == 0, "an answered request is let go");

        // Errors route the same way, whitespace or not.
        final long err = trailingToken(m.outbound(b("[\"x\",\"y\",{}, 9 ]"), 1));
        final TokenMap.Routed e = m.inbound(b("[ \"error\" , " + err + " ,\"no such thing\"]"));
        check(e != null && s(e.frame).equals("[ \"error\" , 9 ,\"no such thing\"]"),
              "an error carries the UI's token: " + (e == null ? null : s(e.frame)));
    }

    // A UI started after one died counts from 1 again.
    private static void twoConnectionsSendingTheSameTokenStayApart() {
        final TokenMap m = new TokenMap();
        final long first = trailingToken(m.outbound(b("[\"a\",\"b\",{},1]"), 1));
        final long second = trailingToken(m.outbound(b("[\"a\",\"b\",{},1]"), 2));
        check(first != second, "tacky sees two tokens, not one twice");

        final TokenMap.Routed toSecond = m.inbound(b("[\"result\"," + second + ",\"two\"]"));
        check(toSecond != null && toSecond.gen == 2 && s(toSecond.frame).equals("[\"result\",1,\"two\"]"),
              "the second connection gets its own reply");
        final TokenMap.Routed toFirst = m.inbound(b("[\"result\"," + first + ",\"one\"]"));
        check(toFirst != null && toFirst.gen == 1 && s(toFirst.frame).equals("[\"result\",1,\"one\"]"),
              "and the first its own");
    }

    private static void aGoneConnectionsRepliesGoNowhere() {
        final TokenMap m = new TokenMap();
        final long dead = trailingToken(m.outbound(b("[\"message\",\"history\",{},3]"), 1));
        final long live = trailingToken(m.outbound(b("[\"account\",\"list\",{},3]"), 2));
        m.forget(1);
        check(m.pendingCount() == 1, "only the gone connection's requests are forgotten");
        check(m.inbound(b("[\"result\"," + dead + ",[]]")) == null, "its late reply is dropped");
        final TokenMap.Routed r = m.inbound(b("[\"result\"," + live + ",[]]"));
        check(r != null && r.gen == 2, "the live one's still arrives");
    }

    private static void whatCarriesNoTokenPassesThrough() {
        final TokenMap m = new TokenMap();
        final byte[] notify = b("[\"chat\",\"setClientData\",{\"data\":\"[1,2]\"}]");
        check(m.outbound(notify, 1) == notify, "a notify leaves unchanged");
        check(m.pendingCount() == 0, "and holds no entry");

        final byte[] event = b("[\"event\",\"message\",\"New\",{\"timestamp\":5}]");
        final TokenMap.Routed ev = m.inbound(event);
        check(ev != null && ev.frame == event && ev.gen == TokenMap.ANY,
              "an event goes to whoever is attached, unchanged");
        check(m.inbound(b("[\"result\",424242,null]")) == null, "an unknown token is dropped");
        check(m.inbound(b("[\"error\",\"word\",\"x\"]")) == null, "a token we never send is dropped");
    }

    private static void tokensAreNeverHandedOutTwice() {
        final TokenMap m = new TokenMap();
        long last = 0;
        for (int gen = 1; gen <= 3; ++gen) {
            for (int ui = 1; ui <= 3; ++ui) {
                final long t = trailingToken(m.outbound(b("[\"a\",\"b\",{}," + ui + "]"), gen));
                check(t > last, "token " + t + " follows " + last);
                last = t;
                m.inbound(b("[\"result\"," + t + ",0]"));
            }
            m.forget(gen);
        }
    }

    private static long trailingToken(byte[] frame) {
        final String f = s(frame).trim();
        final int comma = f.lastIndexOf(',');
        return Long.parseLong(f.substring(comma + 1, f.length() - 1).trim());
    }

    private static byte[] b(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    private static String s(byte[] b) {
        return new String(b, StandardCharsets.UTF_8);
    }

    private static void check(boolean ok, String what) {
        if (!ok) {
            ++failures;
            System.err.println("FAIL: " + what);
        }
    }
}
