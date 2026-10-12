package org.qtproject.example.quackchat;

import android.annotation.TargetApi;
import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;
import android.util.Log;

import java.util.List;

/**
 * Call audio routing: the in-call mode and the communication device. Voice
 * starts on the earpiece (or a headset), video on the speaker.
 */
final class CallAudio {
    private static final String TAG = "quack.callaudio";

    private final AudioManager am;
    private boolean inCall;
    private boolean speaker;

    CallAudio(Context ctx) {
        am = ctx.getSystemService(AudioManager.class);
    }

    /** Idempotent: the first call's choice stands. */
    void start(boolean video) {
        if (inCall) {
            return;
        }
        inCall = true;
        am.setMode(AudioManager.MODE_IN_COMMUNICATION);
        setSpeaker(video);
    }

    void stop() {
        if (!inCall) {
            return;
        }
        inCall = false;
        speaker = false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            am.clearCommunicationDevice();
        } else {
            am.setSpeakerphoneOn(false);
        }
        am.setMode(AudioManager.MODE_NORMAL);
    }

    boolean speakerOn() {
        return speaker;
    }

    /** Whether the proximity sensor should blank the screen. */
    boolean atEar() {
        if (!inCall || speaker) {
            return false;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            final AudioDeviceInfo d = am.getCommunicationDevice();
            return d == null || d.getType() == AudioDeviceInfo.TYPE_BUILTIN_EARPIECE;
        }
        return !am.isBluetoothScoOn() && !am.isWiredHeadsetOn();
    }

    void setSpeaker(boolean on) {
        speaker = on;
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            am.setSpeakerphoneOn(on);
            return;
        }
        final AudioDeviceInfo d = on ? pick(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) : earpieceOrHeadset();
        if (d == null || !am.setCommunicationDevice(d)) {
            Log.w(TAG, "could not route the call to " + (on ? "the speaker" : "the earpiece"));
        }
    }

    @TargetApi(Build.VERSION_CODES.S)
    private AudioDeviceInfo earpieceOrHeadset() {
        for (int type : new int[] {
                AudioDeviceInfo.TYPE_BLE_HEADSET,
                AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
                AudioDeviceInfo.TYPE_WIRED_HEADSET,
                AudioDeviceInfo.TYPE_USB_HEADSET,
                AudioDeviceInfo.TYPE_BUILTIN_EARPIECE}) {
            final AudioDeviceInfo d = pick(type);
            if (d != null) {
                return d;
            }
        }
        return null;
    }

    @TargetApi(Build.VERSION_CODES.S)
    private AudioDeviceInfo pick(int type) {
        final List<AudioDeviceInfo> devices = am.getAvailableCommunicationDevices();
        for (AudioDeviceInfo d : devices) {
            if (d.getType() == type) {
                return d;
            }
        }
        return null;
    }
}
