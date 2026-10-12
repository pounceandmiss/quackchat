package org.qtproject.example.quackchat;

import android.content.Context;
import android.graphics.PixelFormat;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Shows one of tacky's frame streams (layout in tacky's doc/DOC.md), as
 * FrameStream and VideoSurface do on the Qt side. I420 goes to the GPU as is.
 * A header that does not parse leaves no way to resync, so the reader stops.
 */
public final class FrameStreamView extends GLSurfaceView {
    private static final String TAG = "quack.video";

    private static final int MAGIC = 0x31465654;       // 'T','V','F','1'
    private static final int VERSION = 1;
    private static final int HEADER_BYTES = 40;
    private static final int FORMAT_I420 = 0x30323449; // 'I','4','2','0'
    private static final int MAX_PAYLOAD = 64 * 1024 * 1024;
    private static final int MAX_SIDE = 16384;

    private final Renderer renderer = new Renderer();
    private Reader reader;
    /** Kept across a detach, so reading resumes on reattach. */
    private String path;

    public FrameStreamView(Context ctx) {
        this(ctx, null);
    }

    public FrameStreamView(Context ctx, AttributeSet attrs) {
        super(ctx, attrs);
        setEGLContextClientVersion(2);
        // Alpha for rounded corners: a SurfaceView ignores view clipping.
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setRenderer(renderer);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
    }

    /** Mirrored, cropped and rounded. */
    void setSelfView(boolean selfView) {
        renderer.mirror = selfView;
        renderer.crop = selfView;
        renderer.radius = selfView ? 12 * getResources().getDisplayMetrics().density : 0f;
        requestRender();
    }

    /** Cropped and rounded, for a grid tile. */
    void setTile(float radiusPx) {
        renderer.crop = true;
        renderer.radius = radiusPx;
        requestRender();
    }

    void setCrop(boolean crop) {
        renderer.crop = crop;
        requestRender();
    }

    /** Reads {@code path} from now on; null stops reading and blanks the view. */
    void show(String path) {
        if (path == null ? this.path == null : path.equals(this.path)) {
            return;
        }
        this.path = path;
        stopReading();
        renderer.clear();
        requestRender();
        if (isAttachedToWindow()) {
            startReading();
        }
    }

    private void startReading() {
        if (path != null && reader == null) {
            reader = new Reader(path);
            reader.start();
        }
    }

    private void stopReading() {
        if (reader != null) {
            reader.close();
            reader = null;
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        // The stream re-sends its newest frame on connect.
        startReading();
    }

    @Override
    protected void onDetachedFromWindow() {
        stopReading();
        super.onDetachedFromWindow();
    }

    private static int le32(byte[] b, int off) {
        return (b[off] & 0xff) | (b[off + 1] & 0xff) << 8
                | (b[off + 2] & 0xff) << 16 | (b[off + 3] & 0xff) << 24;
    }

    private static int le16(byte[] b, int off) {
        return (b[off] & 0xff) | (b[off + 1] & 0xff) << 8;
    }

    static int i420Bytes(int w, int h) {
        final int cw = (w + 1) / 2;
        final int ch = (h + 1) / 2;
        return w * h + 2 * cw * ch;
    }

    private final class Reader extends Thread {
        final String path;
        private final LocalSocket sock = new LocalSocket();
        private volatile boolean closed;

        Reader(String path) {
            super("quack-video");
            this.path = path;
        }

        void close() {
            closed = true;
            try {
                sock.shutdownInput();
            } catch (IOException ignored) {
                // not connected yet; closing below is enough
            }
            try {
                sock.close();
            } catch (IOException ignored) {
                // going away regardless
            }
        }

        @Override
        public void run() {
            try {
                sock.connect(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM));
                final InputStream raw = sock.getInputStream();
                final DataInputStream in = new DataInputStream(new BufferedInputStream(raw, 1 << 16));
                final byte[] head = new byte[HEADER_BYTES];
                byte[] spare = null;
                while (!closed) {
                    in.readFully(head);
                    final int hlen = le16(head, 6);
                    final int w = le32(head, 16);
                    final int h = le32(head, 20);
                    final int plen = le32(head, 24);
                    if (le32(head, 0) != MAGIC || le16(head, 4) != VERSION
                            || hlen < HEADER_BYTES || le32(head, 8) != FORMAT_I420
                            || w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE
                            || plen < 0 || plen > MAX_PAYLOAD || plen != i420Bytes(w, h)) {
                        Log.w(TAG, "frame stream " + path + " sent a malformed header");
                        return;
                    }
                    in.skipBytes(hlen - HEADER_BYTES);
                    final byte[] buf = spare != null && spare.length == plen ? spare : new byte[plen];
                    in.readFully(buf);
                    spare = renderer.offer(buf, w, h);
                    requestRender();
                }
            } catch (IOException e) {
                if (!closed) {
                    Log.i(TAG, "frame stream " + path + " ended: " + e.getMessage());
                }
            } finally {
                try {
                    sock.close();
                } catch (IOException ignored) {
                    // already gone
                }
            }
        }
    }

    private static final class Renderer implements GLSurfaceView.Renderer {
        private static final String VERTEX =
                "attribute vec2 pos;\n"
                + "attribute vec2 uv;\n"
                + "varying vec2 v_uv;\n"
                + "void main() { gl_Position = vec4(pos, 0.0, 1.0); v_uv = uv; }\n";
        // BT.601, limited range: what libwebrtc and rtc-mv hand out.
        private static final String FRAGMENT =
                "precision mediump float;\n"
                + "varying vec2 v_uv;\n"
                + "uniform sampler2D ty, tu, tv;\n"
                + "uniform vec2 size;\n"
                + "uniform float radius;\n"
                + "void main() {\n"
                + "  float y = 1.164 * (texture2D(ty, v_uv).r - 0.0625);\n"
                + "  float u = texture2D(tu, v_uv).r - 0.5;\n"
                + "  float v = texture2D(tv, v_uv).r - 0.5;\n"
                + "  vec3 rgb = vec3(y + 1.596 * v, y - 0.391 * u - 0.813 * v, y + 2.018 * u);\n"
                // Rounded corners, antialiased over a pixel.
                + "  float a = 1.0;\n"
                + "  if (radius > 0.0) {\n"
                + "    vec2 h = size * 0.5;\n"
                + "    vec2 q = abs(gl_FragCoord.xy - h) - (h - vec2(radius));\n"
                + "    a = clamp(0.5 - (length(max(q, 0.0)) - radius), 0.0, 1.0);\n"
                + "  }\n"
                + "  gl_FragColor = vec4(rgb * a, a);\n"
                + "}\n";

        volatile boolean mirror;
        volatile boolean crop;
        volatile float radius;

        private final Object lock = new Object();
        private byte[] pending;
        private int pendingW, pendingH;
        private boolean cleared;
        /** A consumed buffer, for the reader to refill. */
        private byte[] free;

        private ByteBuffer planes;
        private int frameW, frameH;
        /** Same size again refills the textures instead of reallocating. */
        private int texW, texH;
        private int viewW, viewH;
        private int program;
        private int posLoc, uvLoc, sizeLoc, radiusLoc;
        private final int[] samplerLoc = new int[3];
        private final int[] tex = new int[3];
        private final float[] corners = new float[16];
        private final FloatBuffer quad = ByteBuffer.allocateDirect(16 * 4)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();

        /** Reader thread: hands over a frame and gets a buffer back to reuse. */
        byte[] offer(byte[] buf, int w, int h) {
            synchronized (lock) {
                byte[] back = pending;
                pending = buf;
                pendingW = w;
                pendingH = h;
                if (back == null) {
                    back = free;
                    free = null;
                }
                return back;
            }
        }

        void clear() {
            synchronized (lock) {
                pending = null;
                cleared = true;
            }
        }

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            program = link(VERTEX, FRAGMENT);
            posLoc = GLES20.glGetAttribLocation(program, "pos");
            uvLoc = GLES20.glGetAttribLocation(program, "uv");
            sizeLoc = GLES20.glGetUniformLocation(program, "size");
            radiusLoc = GLES20.glGetUniformLocation(program, "radius");
            final String[] names = {"ty", "tu", "tv"};
            for (int i = 0; i < 3; ++i) {
                samplerLoc[i] = GLES20.glGetUniformLocation(program, names[i]);
            }
            GLES20.glGenTextures(3, tex, 0);
            for (int t : tex) {
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
            }
            GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
            frameW = frameH = 0; // a new context has none of the old textures
            texW = texH = 0;
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int w, int h) {
            viewW = w;
            viewH = h;
            GLES20.glViewport(0, 0, w, h);
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            synchronized (lock) {
                if (cleared) {
                    cleared = false;
                    frameW = frameH = 0;
                }
                if (pending != null) {
                    if (planes == null || planes.capacity() < pending.length) {
                        planes = ByteBuffer.allocateDirect(pending.length);
                    }
                    planes.clear();
                    planes.put(pending);
                    frameW = pendingW;
                    frameH = pendingH;
                    free = pending;
                    pending = null;
                    upload();
                }
            }
            GLES20.glClearColor(0f, 0f, 0f, 0f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
            if (frameW == 0 || viewW == 0) {
                return;
            }
            draw();
        }

        private void upload() {
            final int cw = (frameW + 1) / 2;
            final int ch = (frameH + 1) / 2;
            final boolean refill = frameW == texW && frameH == texH;
            int off = 0;
            for (int i = 0; i < 3; ++i) {
                final int w = i == 0 ? frameW : cw;
                final int h = i == 0 ? frameH : ch;
                planes.position(off);
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0 + i);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex[i]);
                if (refill) {
                    GLES20.glTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, planes);
                } else {
                    GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE,
                            w, h, 0, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, planes);
                }
                off += w * h;
            }
            texW = frameW;
            texH = frameH;
        }

        private void draw() {
            final float frameAspect = (float) frameW / frameH;
            final float viewAspect = (float) viewW / viewH;
            // Half-extents in clip space.
            float sx = 1f;
            float sy = 1f;
            final boolean wider = frameAspect > viewAspect;
            if (crop && wider) {
                sx = frameAspect / viewAspect;   // full height, sides cut off
            } else if (crop) {
                sy = viewAspect / frameAspect;   // full width, top and bottom cut
            } else if (wider) {
                sy = viewAspect / frameAspect;   // full width, bars above and below
            } else {
                sx = frameAspect / viewAspect;   // full height, bars at the sides
            }
            final float l = mirror ? 1f : 0f;
            final float r = mirror ? 0f : 1f;
            final float[] c = corners;
            c[0] = -sx; c[1] = -sy; c[2] = l;  c[3] = 1f;
            c[4] = sx;  c[5] = -sy; c[6] = r;  c[7] = 1f;
            c[8] = -sx; c[9] = sy;  c[10] = l; c[11] = 0f;
            c[12] = sx; c[13] = sy; c[14] = r; c[15] = 0f;
            quad.clear();
            quad.put(c);
            GLES20.glUseProgram(program);
            quad.position(0);
            GLES20.glVertexAttribPointer(posLoc, 2, GLES20.GL_FLOAT, false, 16, quad);
            GLES20.glEnableVertexAttribArray(posLoc);
            quad.position(2);
            GLES20.glVertexAttribPointer(uvLoc, 2, GLES20.GL_FLOAT, false, 16, quad);
            GLES20.glEnableVertexAttribArray(uvLoc);
            for (int i = 0; i < 3; ++i) {
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0 + i);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex[i]);
                GLES20.glUniform1i(samplerLoc[i], i);
            }
            GLES20.glUniform2f(sizeLoc, viewW, viewH);
            GLES20.glUniform1f(radiusLoc, radius);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        }

        private static int link(String vs, String fs) {
            final int p = GLES20.glCreateProgram();
            GLES20.glAttachShader(p, compile(GLES20.GL_VERTEX_SHADER, vs));
            GLES20.glAttachShader(p, compile(GLES20.GL_FRAGMENT_SHADER, fs));
            GLES20.glLinkProgram(p);
            final int[] ok = new int[1];
            GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0);
            if (ok[0] == 0) {
                Log.e(TAG, "link failed: " + GLES20.glGetProgramInfoLog(p));
            }
            return p;
        }

        private static int compile(int type, String src) {
            final int s = GLES20.glCreateShader(type);
            GLES20.glShaderSource(s, src);
            GLES20.glCompileShader(s);
            final int[] ok = new int[1];
            GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0);
            if (ok[0] == 0) {
                Log.e(TAG, "compile failed: " + GLES20.glGetShaderInfoLog(s));
            }
            return s;
        }
    }
}
