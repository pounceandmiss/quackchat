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

    // The `avatar visible` JIDs in `spy`, in order, for one account.
    static QStringList visibleJids(const QSignalSpy &spy, const QString &acc) {
        QStringList jids;
        for (const QList<QVariant> &call : spy) {
            if (call.at(0).toString() != QLatin1String("avatar") ||
                call.at(1).toString() != QLatin1String("visible"))
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

    // With no backend wired, fetch() completes the sink as a failure rather than
    // leaving it hanging.
    void fetchWithoutBackendFails() {
        AvatarController c; // no setBackend()
        AvatarResponse r;
        c.fetch("me@h", "bob@h", "somehash", &r);
        QVERIFY(!r.errorString().isEmpty());
    }

    // tacky drops its visible set on every <Disconnect>, so a reconnect has to
    // re-send the subscriptions or ensureVisible() short-circuits on its own
    // bookkeeping and the hashes never update again.
    void resubscribesOnReady() {
        TackyBackend backend; // in-memory session
        QVERIFY(backend.start());
        AvatarController c;
        c.setBackend(&backend);

        QSignalSpy sent(&backend, &TackyBackend::sent);
        c.hashFor("me@h", "bob@h"); // the read is what subscribes
        QCOMPARE(visibleJids(sent, "me@h"), QStringList{"bob@h"});

        // A second read must not re-ask while the subscription still stands.
        sent.clear();
        c.hashFor("me@h", "bob@h");
        QCOMPARE(visibleJids(sent, "me@h"), QStringList{});

        sent.clear();
        c.handleEvent("conn", "Ready", QVariantMap{{"acc", "me@h"}});
        QCOMPARE(visibleJids(sent, "me@h"), QStringList{"bob@h"});
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
        c.handleEvent("conn", "Ready", QVariantMap{{"acc", "me@h"}});
        QCOMPARE(visibleJids(sent, "me@h"), QStringList{"bob@h"});
        QCOMPARE(visibleJids(sent, "other@h"), QStringList{});
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
