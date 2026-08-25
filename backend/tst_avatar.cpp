// Covers hash tracking from `avatar <Update>` events and the base64->image
// decode in the sink; fetch() is only checked for its no-backend guard.
#include <QtTest>

#include "AvatarController.h"
#include "AvatarImageProvider.h" // AvatarResponse (the AvatarSink implementer)
#include "TackyBackend.h"

class TestAvatar : public QObject {
    Q_OBJECT

    // A valid 1x1 PNG, base64-encoded, exactly as tacky's `avatar thumb` hands
    // it over the JSON transport.
    static QByteArray onePixelPngB64() {
        return "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4"
               "z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC";
    }

    static QVariantMap update(const QString &acc, const QString &jid,
                              const QString &hash) {
        return {{"acc", acc}, {"jid", jid}, {"hash", hash}};
    }

    // The JIDs `spy` saw an `avatar <method>` call for, in order, for one
    // account.
    static QStringList jidsAsked(const QSignalSpy &spy, const QString &method,
                                 const QString &acc) {
        QStringList jids;
        for (const QList<QVariant> &call : spy) {
            if (call.at(0).toString() != QLatin1String("avatar") ||
                call.at(1).toString() != method)
                continue;
            const QVariantMap a = call.at(2).toMap();
            if (a.value("acc").toString() == acc)
                jids.append(a.value("jid").toString());
        }
        return jids;
    }

private slots:
    // <Update> (bare "Update" on the JSON wire) sets/replaces the hash and bumps
    // rev; an empty hash clears it (changed away / disabled / none).
    void tracksHash() {
        AvatarController c;
        QSignalSpy spy(&c, &AvatarController::changed);
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString()); // unknown -> ""

        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "abc"));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("abc"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.rev() > 0);

        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "def"));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("def")); // replaced

        c.handleEvent("avatar", "Update", update("me@h", "bob@h", ""));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString()); // cleared
    }

    // Hashes are per (acc,jid); unrelated modules/events are ignored.
    void keysAndFilters() {
        AvatarController c;
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "h1"));
        c.handleEvent("avatar", "Update", update("other@h", "bob@h", "h2"));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("h1"));
        QCOMPARE(c.hashFor("other@h", "bob@h"), QString("h2"));

        const int rev = c.rev();
        c.handleEvent("avatar", "Progress", update("me@h", "bob@h", "x"));
        c.handleEvent("message", "New", QVariantMap{{"acc", "me@h"}});
        QCOMPARE(c.rev(), rev); // neither touched anything
    }

    // A group chat's ?join suffix is not part of the avatar's JID, so an update
    // for "room@muc?join" is readable as "room@muc".
    void stripsJoinSuffix() {
        AvatarController c;
        c.handleEvent("avatar", "Update", update("me@h", "room@muc?join", "g"));
        QCOMPARE(c.hashFor("me@h", "room@muc"), QString("g"));
        QCOMPARE(c.hashFor("me@h", "room@muc?join"), QString("g"));
    }

    // The sink decodes real base64 PNG bytes to a usable image (no error), and
    // reports an error for the empty "no avatar" reply so QML shows its fallback.
    void decodesPng() {
        AvatarResponse ok;
        ok.deliverBase64(onePixelPngB64());
        QVERIFY2(ok.errorString().isEmpty(), qPrintable(ok.errorString()));
        QVERIFY(ok.textureFactory() != nullptr);

        AvatarResponse none;
        none.deliverBase64(QByteArray()); // "" == no avatar
        QVERIFY(!none.errorString().isEmpty());

        AvatarResponse junk;
        junk.deliverBase64("bm90IGEgcG5n"); // "not a png"
        QVERIFY(!junk.errorString().isEmpty());
    }

    // The texture is cut to the size the Image asked for, so a hero-sized
    // avatar is not served the same 96px one a list row gets. An Image that
    // asks for nothing still gets the small default, and no caller can pin a
    // full-resolution photo in the cache.
    void sizesTheTextureToTheRequest() {
        QCOMPARE(AvatarResponse::edgeFor(QSize(224, 224)), 224);
        QCOMPARE(AvatarResponse::edgeFor(QSize()), 96);       // no sourceSize
        QCOMPARE(AvatarResponse::edgeFor(QSize(0, 88)), 88);  // width-only bind
        QCOMPARE(AvatarResponse::edgeFor(QSize(4096, 4096)), 256);
    }

    // With no backend wired, fetch() completes the sink as a failure rather than
    // leaving it hanging.
    void fetchWithoutBackendFails() {
        AvatarController c; // no setBackend()
        AvatarResponse r;
        c.fetch("me@h", "bob@h", "somehash", &r);
        QVERIFY(!r.errorString().isEmpty());
    }

    // Subscribing is the mark and nothing else: tacky re-emits an <Update> from
    // its cache for every one, so there is no `avatar metadata` read to make.
    void subscribesWithTheMarkAlone() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        QSignalSpy sent(&backend, &TackyBackend::sent);
        c.hashFor("me@h", "bob@h");
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{"bob@h"});
        QCOMPARE(jidsAsked(sent, "metadata", "me@h"), QStringList{});

        // One mark per JID, not one per binding evaluation.
        sent.clear();
        c.hashFor("me@h", "bob@h");
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{});
    }

    // A JID starts unknown, and the <Update> the mark asked for fills it in.
    void learnsTheHashFromAnUpdate() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        QCOMPARE(c.hashFor("me@h", "bob@h"), QString()); // marks, knows nothing yet
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "abc"));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("abc"));
    }

    // Nothing times out an avatar request, so a backend that stops has to fail
    // the waiting sinks or the QQuickImageResponse never completes.
    void pendingSinksFailWhenBackendStops() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        AvatarResponse r;
        c.fetch("me@h", "bob@h", "somehash", &r);
        QVERIFY(r.errorString().isEmpty()); // still in flight

        backend.stop();
        QVERIFY(!r.errorString().isEmpty());
    }
};

QTEST_MAIN(TestAvatar)
#include "tst_avatar.moc"
