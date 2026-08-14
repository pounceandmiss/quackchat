// The avatar layer, exposed to QML as `App.avatars`. QQuickPixmapCache already
// decodes and caches by URL, so this only covers what it can't: tracking the
// current hash per (acc,jid) for cache invalidation, and bridging the async
// `avatar data` fetch to an AvatarSink.
//
// Hashes arrive two ways: `avatar <Update>` pushes every change, and an
// `avatar metadata` read answers for what the backend held before we started.
// The read is what a frontend outlived by its backend needs - on Android the
// interpreter belongs to a service the activity does not take with it.
//
// Core/Qml only, no QImage - the sink does the decoding, so the headless model
// tests never pull in QtGui.
#ifndef AVATARCONTROLLER_H
#define AVATARCONTROLLER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class TackyBackend;
class AvatarSink;

class AvatarController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // Bumped on every avatar change. QML reads it purely to give the image-URL
    // bindings a dependency, so they re-run hashFor() on an update.
    Q_PROPERTY(int rev READ rev NOTIFY changed)

public:
    explicit AvatarController(QObject *parent = nullptr);

    void setBackend(TackyBackend *backend);

    int rev() const { return m_rev; }

    // The current hash for a JID, or "" if none is known - not the image, just
    // the token QML mixes into the URL to cache-bust. The first call for a JID
    // also marks it `visible`, which is what makes the backend fetch it and
    // push an `avatar <Update>` carrying the hash.
    Q_INVOKABLE QString hashFor(const QString &acc, const QString &jid);

    // Re-ask the server for this JID's avatar, ignoring the cached hash - the
    // way out when a contact changed their picture and we never heard about it.
    // The new hash arrives as an ordinary `avatar <Update>`, so the picture
    // swaps itself; there is nothing to wait for here.
    Q_INVOKABLE void refresh(const QString &acc, const QString &jid);

    // Completes `sink` when the reply lands or fails. Called from the image
    // provider's loader thread, so it hops onto this object's thread first.
    void fetch(const QString &acc, const QString &jid, const QString &hash,
               AvatarSink *sink);

    // Public so tests can drive hash tracking with canned events.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);

signals:
    void changed();

private slots:
    void onResult(int token, const QVariant &data);
    void onError(int token, const QString &message);
    // Drops the visible set and fails the waiting sinks: nothing times a fetch
    // out, so an image response left pending here never completes.
    void onRunningChanged();

private:
    static QString key(const QString &acc, const QString &jid);
    static QString normJid(const QString &jid);
    void ensureVisible(const QString &acc, const QString &jid);
    // Records the hash and tells QML, from either route.
    void applyHash(const QString &k, const QString &hash);
    // tacky keeps its visible marks across a <Disconnect>, so a reconnect
    // re-primes nothing on its own. Re-read the hashes instead: one may have
    // moved while `acc` was offline, with no one here to hear the <Update>.
    void resubscribe(const QString &acc);

    TackyBackend *m_backend = nullptr;
    QHash<QString, QString> m_hash;     // "acc\njid" -> hash
    QHash<int, AvatarSink *> m_pending; // request token -> waiting sink
    QHash<int, QString> m_metaPending;  // request token -> "acc\njid"
    QSet<QString> m_visible;
    int m_rev = 0;
};

#endif // AVATARCONTROLLER_H
