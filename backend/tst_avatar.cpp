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

    // Every subscribe carries an `avatar metadata` read: tacky's visible marks
    // outlive this process, so a re-mark is a no-op with no <Update> behind it,
    // and without the read a restarted frontend has no hash at all.
    void subscribeReadsTheCachedHash() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        QSignalSpy sent(&backend, &TackyBackend::sent);
        c.hashFor("me@h", "bob@h");
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{"bob@h"});
        QCOMPARE(jidsAsked(sent, "metadata", "me@h"), QStringList{"bob@h"});

        // One read per subscription, not one per binding evaluation.
        sent.clear();
        c.hashFor("me@h", "bob@h");
        QCOMPARE(jidsAsked(sent, "metadata", "me@h"), QStringList{});
    }

    // A reconnect re-reads what it was showing: tacky keeps its visible marks,
    // so nothing re-primes on its own, and a hash that moved while the account
    // was offline had no one here to hear the <Update>.
    void resubscribesOnReady() {
        TackyBackend backend; // in-memory session
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        QSignalSpy sent(&backend, &TackyBackend::sent);
        c.hashFor("me@h", "bob@h"); // the read is what subscribes
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{"bob@h"});

        // A second read must not re-ask while the subscription still stands.
        sent.clear();
        c.hashFor("me@h", "bob@h");
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{});

        sent.clear();
        c.handleEvent("conn", "State",
                      QVariantMap{{"acc", "me@h"}, {"state", "connected"}});
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{"bob@h"});
        QCOMPARE(jidsAsked(sent, "metadata", "me@h"), QStringList{"bob@h"});
    }

    // <Ready> for one account must not disturb another's subscriptions.
    void resubscribeIsPerAccount() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);
        c.hashFor("me@h", "bob@h");
        c.hashFor("other@h", "eve@h");

        QSignalSpy sent(&backend, &TackyBackend::sent);
        c.handleEvent("conn", "State",
                      QVariantMap{{"acc", "me@h"}, {"state", "connected"}});
        QCOMPARE(jidsAsked(sent, "visible", "me@h"), QStringList{"bob@h"});
        QCOMPARE(jidsAsked(sent, "visible", "other@h"), QStringList{});
    }

    // `avatar metadata` reads the metadata row alone, so it can hand over a hash
    // whose bytes tacky has not fetched yet. The empty `data` reply that follows
    // has to unstick the hash: QML would otherwise keep an Image pointed at the
    // one URL it has already failed on, and never reload it when the bytes land.
    void forgetsAHashTheBackendCannotServe() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "abc"));

        AvatarResponse r;
        c.fetch("me@h", "bob@h", "abc", &r);
        // request() hands out tokens in order, so the fetch's is the one before
        // whatever the next request would get.
        const int dataToken =
            backend.request("avatar", "metadata", QVariantMap{}) - 1;
        emit backend.result(dataToken, QString()); // "" == bytes not cached

        QVERIFY(!r.errorString().isEmpty()); // the Image falls back
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString());

        // The hash comes back when the backend's own fetch ends in an <Update>,
        // and QML sees a URL it has not failed on.
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "abc"));
        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("abc"));
    }

    // Only the hash that went unserved is dropped: an <Update> that landed while
    // the fetch was out is newer news than the reply to it.
    void keepsAHashThatMovedDuringTheFetch() {
        TackyBackend backend;
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "abc"));

        AvatarResponse r;
        c.fetch("me@h", "bob@h", "abc", &r);
        c.handleEvent("avatar", "Update", update("me@h", "bob@h", "def"));
        const int dataToken =
            backend.request("avatar", "metadata", QVariantMap{}) - 1;
        emit backend.result(dataToken, QString());

        QCOMPARE(c.hashFor("me@h", "bob@h"), QString("def"));
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
