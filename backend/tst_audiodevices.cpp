// Device/gain state off canned replies, plus a real libtacky round trip - the
// one thing that proves the miniaudio side of the archive is actually linked
// and that the `audio` argument names match the Tcl.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "AudioDevices.h"
#include "TackyBackend.h"

static void feed(AudioDevices &a, const QByteArray &json) {
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    a.handleEvent(arr.at(1).toString(), arr.at(2).toString(),
                  arr.at(3).toVariant());
}

class TestAudioDevices : public QObject {
    Q_OBJECT
private slots:
    void volumeEventDrivesMuted();
    void unmuteReturnsToThePreviousLevel();
    void preferredDeviceEventIsGlobal();
    void refreshesWhenTheBackendConnects();
    // Against a real backend:
    void enumeratesRealDevices();
    void preferencesRoundTripThroughTheBackend();
};

// The device list and the stored prefs are answers, not events, and on Android
// the link is still coming up when AppController first asks.
void TestAudioDevices::refreshesWhenTheBackendConnects() {
    TackyBackend backend;
    AudioDevices a;
    a.setBackend(&backend); // bound before the link is up
    QSignalSpy sent(&backend, &TackyBackend::sent);

    emit backend.connected();

    QStringList asked;
    for (const QList<QVariant> &call : sent)
        asked << call.at(1).toString();
    QCOMPARE(asked, QStringList({"enumerateDevices", "getPreferredDevice",
                                 "getPreferredDevice", "getVolume",
                                 "getVolume"}));
    QCOMPARE(sent.first().at(0).toString(), QString("audio"));
}

void TestAudioDevices::volumeEventDrivesMuted() {
    AudioDevices a;
    QSignalSpy changed(&a, &AudioDevices::captureVolumeChanged);
    QCOMPARE(a.captureVolume(), 1.0); // unity is what an unset gain reads as
    QVERIFY(!a.captureMuted());

    feed(a, R"(["event","audio","Volume",{"kind":"capture","volume":0.0}])");
    QCOMPARE(a.captureVolume(), 0.0);
    QVERIFY(a.captureMuted());
    QCOMPARE(changed.count(), 1);

    // Playback is a separate gain and must not have moved.
    QCOMPARE(a.playbackVolume(), 1.0);
    QVERIFY(!a.playbackMuted());
}

// Mute is not a backend concept: it is gain 0.0, so coming back has to land on
// the level the user was actually running at, not on unity. Needs a real
// backend - the setters deliberately do nothing without one, rather than move
// the UI to a value that could never take effect.
void TestAudioDevices::unmuteReturnsToThePreviousLevel() {
    TackyBackend backend;
    QVERIFY(backend.start());

    AudioDevices a;
    a.setBackend(&backend);
    a.setPlaybackVolume(0.4);
    QCOMPARE(a.playbackVolume(), 0.4);

    a.setPlaybackMuted(true);
    QCOMPARE(a.playbackVolume(), 0.0);
    QVERIFY(a.playbackMuted());

    a.setPlaybackMuted(false);
    QCOMPARE(a.playbackVolume(), 0.4);
    QVERIFY(!a.playbackMuted());

    // And the level that came back is the one the backend now holds.
    AudioDevices readback;
    readback.setBackend(&backend);
    readback.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(qFuzzyCompare(readback.playbackVolume(), 0.4), 5000);

    backend.stop();
}

void TestAudioDevices::preferredDeviceEventIsGlobal() {
    AudioDevices a;
    QSignalSpy changed(&a, &AudioDevices::captureDeviceChanged);
    feed(a, R"(["event","audio","PreferredDevice",
        {"kind":"capture","id":"alsa:hw:1,0"}])");
    QCOMPARE(a.captureDevice(), QString("alsa:hw:1,0"));
    QCOMPARE(changed.count(), 1);

    // Repeating the same id must not churn bindings.
    feed(a, R"(["event","audio","PreferredDevice",
        {"kind":"capture","id":"alsa:hw:1,0"}])");
    QCOMPARE(changed.count(), 1);

    // "" is a real value: whatever the system picks.
    feed(a, R"(["event","audio","PreferredDevice",{"kind":"capture","id":""}])");
    QCOMPARE(a.captureDevice(), QString());
}

// Proves rtcma/miniaudio really is inside libtacky.a and reachable from the
// embedded interpreter. A headless box may genuinely have no cards, so this
// asserts on the shape of the reply rather than on finding hardware.
void TestAudioDevices::enumeratesRealDevices() {
    TackyBackend backend;
    QVERIFY(backend.start());

    QSignalSpy results(&backend, &TackyBackend::result);
    QSignalSpy errors(&backend, &TackyBackend::error);
    const int token = backend.request(QStringLiteral("audio"),
                                      QStringLiteral("enumerateDevices"));

    QTRY_VERIFY_WITH_TIMEOUT(results.count() > 0 || errors.count() > 0, 10000);
    QVERIFY2(errors.isEmpty(),
             qPrintable(errors.isEmpty() ? QString()
                                         : errors.first().at(1).toString()));
    QCOMPARE(results.first().at(0).toInt(), token);
    const QVariantMap m = results.first().at(1).toMap();
    QVERIFY(m.contains("capture"));
    QVERIFY(m.contains("playback"));

    backend.stop();
}

// Round trips the preference through the real `audio` module, which is what
// pins the argument names (kind/id, kind/volume) - the reference has been
// wrong about key names elsewhere.
void TestAudioDevices::preferencesRoundTripThroughTheBackend() {
    TackyBackend backend;
    QVERIFY(backend.start());

    AudioDevices a;
    a.setBackend(&backend);
    a.setCaptureDevice(QStringLiteral("test-mic-id"));
    a.setPlaybackVolume(0.25);

    // Wipe the optimistic local values so what comes back is the backend's.
    AudioDevices readback;
    readback.setBackend(&backend);
    readback.refresh();

    QTRY_VERIFY_WITH_TIMEOUT(
        readback.captureDevice() == QLatin1String("test-mic-id"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(qFuzzyCompare(readback.playbackVolume(), 0.25), 5000);

    backend.stop();
}

QTEST_MAIN(TestAudioDevices)
#include "tst_audiodevices.moc"
