package org.qtproject.example.quackchat;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Process;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Sits between tacky and the Qt UI process, and is the only consumer tacky ever
 * has. It parses the frame <em>envelope</em> only - module, method or event
 * name, and whether a token is present - and treats payloads as opaque bytes.
 * That is enough to do all four of the things it needs to, without a second
 * implementation of a protocol the UI already models in C++:
 *
 * <ul>
 *   <li><b>forward</b> - the default, both directions</li>
 *   <li><b>observe</b> - notify events drive notifications, UI attached or not</li>
 *   <li><b>inject</b> - notification actions write tokenless notify frames</li>
 *   <li><b>renumber</b> - request tokens, through {@link TokenMap}</li>
 * </ul>
 *
 * <p>Two invariants keep tacky safe, since it is single-consumer by
 * construction: the tokens tacky sees are this class's and never reused (see
 * {@link TokenMap}), and it never issues {@code avatar visible} (whose refcount
 * is global, and silently starves a second subscriber).
 */
final class FrameRouter implements TackyNative.FrameSink {
    private static final String TAG = "quack.router";

    interface Listener {
        /** An inbound event frame, already split into module and bare name. */
        void onEvent(String module, String name, byte[] frame);
    }

    private final String socketName;
    private final TackyNative tacky;
    private final Listener listener;

    /** An attached UI and its connection number. */
    private static final class Conn {
        final LocalSocket sock;
        final int gen;

        Conn(LocalSocket sock, int gen) {
            this.sock = sock;
            this.gen = gen;
        }
    }

    /** A frame for the UI, and which connection: TokenMap.ANY for events. */
    private static final class Outgoing {
        final byte[] frame;
        final int gen;

        Outgoing(byte[] frame, int gen) {
            this.frame = frame;
            this.gen = gen;
        }
    }

    /** Frames from tacky, awaiting the writer thread. */
    private final BlockingQueue<Outgoing> outbound = new ArrayBlockingQueue<>(1024);

    /** The attached UI, if any. A later connection displaces an earlier one. */
    private final AtomicReference<Conn> client = new AtomicReference<>();

    private final TokenMap tokens = new TokenMap();
    private int lastGen; // acceptLoop's alone

    private volatile boolean running;
    private LocalServerSocket server;

    FrameRouter(String socketName, Listener listener) {
        this.socketName = socketName;
        this.listener = listener;
        this.tacky = new TackyNative(this);
    }

    boolean start(String[] tacoArgs) {
        running = true;
        if (!tacky.start(tacoArgs)) {
            Log.e(TAG, "tacky failed to start");
            return false;
        }
        new Thread(this::acceptLoop, "quack-accept").start();
        new Thread(this::writeLoop, "quack-write").start();
        return true;
    }

    void stop() {
        running = false;
        closeClient();
        try {
            if (server != null) {
                server.close();
            }
        } catch (IOException ignored) {
            // closing is best-effort; the process is going away regardless
        }
        outbound.clear();
        tacky.stop();
    }

    /**
     * Sends a frame to tacky on someone else's behalf. Callers must pass a
     * three-element array with no token: a reply would have no UI to go to.
     */
    void inject(byte[] tokenlessFrame) {
        tacky.send(tokenlessFrame);
    }

    // --- tacky -> here -------------------------------------------------------

    /** On tacky's backend thread: enqueue only, never block or re-enter. */
    @Override
    public void onFrame(byte[] json) {
        final Envelope env = Envelope.parse(json);
        if (env != null && env.isEvent) {
            listener.onEvent(env.module, env.name, json);
        }
        final TokenMap.Routed routed = tokens.inbound(json);
        if (routed == null) {
            return; // a reply owed to a UI that has gone
        }
        if (!outbound.offer(new Outgoing(routed.frame, routed.gen))) {
            // A UI that has stopped reading must not stall tacky's thread. The
            // models re-sync on the next connected edge, so dropping is safe.
            Log.w(TAG, "outbound queue full, dropping frame");
        }
    }

    private void writeLoop() {
        while (running) {
            final Outgoing out;
            try {
                out = outbound.take();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            }
            final Conn conn = client.get();
            if (conn == null) {
                continue; // nobody attached; the UI re-syncs when it returns
            }
            // Queued for a UI that has since been replaced.
            if (out.gen != TokenMap.ANY && out.gen != conn.gen) {
                continue;
            }
            try {
                Lenpipe.write(conn.sock.getOutputStream(), out.frame);
            } catch (IOException e) {
                Log.i(TAG, "UI write failed, dropping it: " + e.getMessage());
                closeClient();
            }
        }
    }

    // --- UI -> here ----------------------------------------------------------

    private void acceptLoop() {
        try {
            server = new LocalServerSocket(socketName);
        } catch (IOException e) {
            // Someone else holds the name. Failing loudly beats running with a
            // UI that can never attach.
            Log.e(TAG, "cannot bind " + socketName, e);
            return;
        }
        while (running) {
            final LocalSocket sock;
            try {
                sock = server.accept();
            } catch (IOException e) {
                if (running) {
                    Log.w(TAG, "accept failed: " + e.getMessage());
                }
                return;
            }
            // The name is abstract, so any app can connect: only our own UI may.
            if (!fromOurApp(sock)) {
                closeQuietly(sock);
                continue;
            }
            closeClient(); // one UI at a time; the newcomer wins
            final Conn conn = new Conn(sock, ++lastGen);
            client.set(conn);
            new Thread(() -> readLoop(conn), "quack-read").start();
        }
    }

    private static boolean fromOurApp(LocalSocket sock) {
        try {
            final int uid = sock.getPeerCredentials().getUid();
            if (uid == Process.myUid()) {
                return true;
            }
            Log.w(TAG, "refused a connection from uid " + uid);
        } catch (IOException e) {
            Log.w(TAG, "no peer credentials, refusing: " + e.getMessage());
        }
        return false;
    }

    private void readLoop(Conn conn) {
        try {
            // Buffered: the length prefix is read a byte at a time, and on a
            // raw socket stream that is a syscall per digit.
            final InputStream in = new BufferedInputStream(conn.sock.getInputStream());
            byte[] frame;
            while ((frame = Lenpipe.read(in)) != null) {
                dispatchFromUi(conn, frame);
            }
        } catch (IOException e) {
            Log.i(TAG, "UI read ended: " + e.getMessage());
        } finally {
            // Only if this connection is still the current one: a displaced
            // reader must not clear its replacement.
            client.compareAndSet(conn, null);
            tokens.forget(conn.gen);
            closeQuietly(conn.sock);
        }
    }

    /**
     * A throwing handler must not kill the read loop - the connection would go
     * deaf and every later request would hang with no reply, exactly as
     * lenpipe.tcl warns about its own drain loop.
     */
    private void dispatchFromUi(Conn conn, byte[] frame) {
        // A displaced reader still draining: nobody would get the replies.
        if (client.get() != conn) {
            return;
        }
        try {
            tacky.send(tokens.outbound(frame, conn.gen));
        } catch (RuntimeException e) {
            Log.e(TAG, "dropping unhandled frame", e);
        }
    }

    private void closeClient() {
        final Conn old = client.getAndSet(null);
        if (old != null) {
            tokens.forget(old.gen);
            closeQuietly(old.sock);
        }
    }

    private static void closeQuietly(LocalSocket sock) {
        try {
            sock.close();
        } catch (IOException ignored) {
            // nothing useful to do; the reader is already unwinding
        }
    }

    /**
     * Just enough of a frame to route it. Deliberately not a JSON parse of the
     * payload: the point is to stay out of the protocol's business.
     */
    static final class Envelope {
        final String module;
        final String name; // method for outbound, bare event name for inbound
        final boolean isEvent;

        private Envelope(String module, String name, boolean isEvent) {
            this.module = module;
            this.name = name;
            this.isEvent = isEvent;
        }

        /**
         * Inbound events are ["event",module,name,args]; everything the UI
         * sends is [module,method,args(,token)]. Results and errors carry a
         * token where a module name would be and route by it, so they are of no
         * interest here and parse to null.
         *
         * <p>Reads the leading strings straight out of the bytes. Decoding the
         * whole frame would copy every avatar blob and history page through a
         * String on its way to being forwarded unchanged.
         */
        static Envelope parse(byte[] frame) {
            final String[] head = new String[3];
            final int n = leadingStrings(frame, head);
            if (n < 2) {
                return null;
            }
            if ("event".equals(head[0])) {
                return n < 3 ? null : new Envelope(head[1], head[2], true);
            }
            if ("result".equals(head[0]) || "error".equals(head[0])) {
                return null;
            }
            return new Envelope(head[0], head[1], false);
        }

        /**
         * Fills {@code out} with the array's leading quoted elements and returns
         * how many were found, stopping at the first element that is not a
         * plain string - which is always the args object. Module, method and
         * event names are ASCII and never escaped, so anything else ends the
         * scan rather than being decoded.
         */
        private static int leadingStrings(byte[] f, String[] out) {
            int i = 0;
            while (i < f.length && f[i] != '[') {
                ++i;
            }
            ++i;
            int found = 0;
            while (found < out.length) {
                while (i < f.length && (f[i] == ' ' || f[i] == ',')) {
                    ++i;
                }
                if (i >= f.length || f[i] != '"') {
                    return found;
                }
                final int start = ++i;
                while (i < f.length && f[i] != '"' && f[i] != '\\') {
                    ++i;
                }
                if (i >= f.length || f[i] != '"') {
                    return found; // unterminated, or escaped where we expect none
                }
                out[found++] = new String(f, start, i - start, StandardCharsets.UTF_8);
                ++i;
            }
            return found;
        }
    }
}
