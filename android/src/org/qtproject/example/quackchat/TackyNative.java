package org.qtproject.example.quackchat;

/**
 * The tacky interpreter, linked into this process. Frames cross as raw UTF-8
 * bytes rather than String: JNI's string calls speak modified UTF-8, which
 * mangles anything outside the BMP, and chat messages are full of emoji.
 */
final class TackyNative {
    static {
        System.loadLibrary("tackyjni");
    }

    /** Receives one frame, on tacky's own backend thread. */
    interface FrameSink {
        /** Must not block: tacky's emit callback is waiting on it. */
        void onFrame(byte[] json);
    }

    private long handle;
    private final FrameSink sink;

    TackyNative(FrameSink sink) {
        this.sink = sink;
    }

    /**
     * Starts the interpreter. Returns as soon as requests can be queued - taco
     * initialises asynchronously, and a failure arrives later as
     * ["event","backend","Dead",{...}] rather than from here.
     */
    synchronized boolean start(String[] tacoArgs) {
        if (handle != 0) {
            return true;
        }
        handle = nativeCreate(tacoArgs, sink);
        return handle != 0;
    }

    synchronized void send(byte[] json) {
        if (handle != 0) {
            nativeSend(handle, json);
        }
    }

    /** Joins tacky's thread; no frame arrives after this returns. */
    synchronized void stop() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private static native long nativeCreate(String[] tacoArgs, FrameSink sink);

    private static native void nativeSend(long handle, byte[] json);

    private static native void nativeDestroy(long handle);
}
