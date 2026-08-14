// One page-able `message search` for an account, scoped to a chat or to all of
// them. tacky owns the matching, the cursor format and the local/remote split;
// this holds the answer and the three things its contract makes the caller's:
// the cursor, the request tag, and which source each leg asks for.
//
// Newest-first, like the store returns them. Results are deliberately not chat
// content - they carry no formatting spans and no attachments, just enough to
// recognise a hit and jump to it.
//
// A search replaces the last one's results when its own arrive rather than when
// it is asked for: the callers re-search on every pause in typing, and a list
// that emptied itself in between would spend most of a typed word blank.
#ifndef SEARCHMODEL_H
#define SEARCHMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class SearchModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    // Empty searches every chat in the account. That is local-only - MAM queries
    // one archive - so it also forces remoteAvailable false.
    Q_PROPERTY(QString chat READ chat WRITE setChat NOTIFY chatChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // Asked for; only acted on where the archive can actually run the search.
    Q_PROPERTY(bool alsoRemote READ alsoRemote WRITE setAlsoRemote NOTIFY alsoRemoteChanged)
    Q_PROPERTY(bool remoteAvailable READ remoteAvailable NOTIFY remoteAvailableChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // No more pages behind this one.
    Q_PROPERTY(bool complete READ complete NOTIFY completeChanged)
    // A search has run, so an empty list means "no hits" rather than "not asked".
    Q_PROPERTY(bool searched READ searched NOTIFY searchedChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY failedChanged)
    // The distinct chats the current results came from, in first-seen order.
    // A name cache is per chat, so an account-wide view needs one each.
    Q_PROPERTY(QStringList resultChats READ resultChats NOTIFY resultChatsChanged)
    // For the views that walk the hits rather than list them, and so have no
    // ListView to read a count off.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        ChatJidRole, // which chat the hit came from; the view's jump target
        FromRole,
        OutgoingRole,
        BodyRole,
        SnippetRole, // BodyRole as one line of rich text, matches bolded
        MatchesRole, // where the query matched, as tacky reported it
        RawRole,
    };
    Q_ENUM(Role)

    explicit SearchModel(QObject *parent = nullptr);
    ~SearchModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString chat() const { return m_chat; }
    QString query() const { return m_query; }
    bool alsoRemote() const { return m_alsoRemote; }
    bool remoteAvailable() const { return m_remoteAvailable; }
    bool searching() const { return m_token != 0; }
    bool complete() const { return m_complete; }
    bool searched() const { return m_searched; }
    bool failed() const { return m_failed; }
    QStringList resultChats() const { return m_resultChats; }
    int count() const { return m_msgs.size(); }

    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setChat(const QString &chat);
    void setQuery(const QString &query);
    void setAlsoRemote(bool v);

    // Run `query` from the top, replacing whatever is displayed.
    Q_INVOKABLE void search();
    // The page behind the last one. Always local: tacky's remote leg skips
    // itself once a cursor is passed, and the cursor it hands back is a store
    // cursor either way.
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void clear();
    // The hit at `row`, for stepping through them without a delegate to read.
    // 0 and empty for a row that does not exist.
    Q_INVOKABLE qlonglong timestampAt(int row) const;
    Q_INVOKABLE QVariantList matchesAt(int row) const;

    // Routing and transforms are public so tests can drive them with canned data.
    void handleResult(int token, const QVariant &data);
    void applyResult(const QVariantMap &result, bool append);

signals:
    void backendChanged();
    void accountChanged();
    void chatChanged();
    void queryChanged();
    void alsoRemoteChanged();
    void remoteAvailableChanged();
    void searchingChanged();
    void completeChanged();
    void searchedChanged();
    void failedChanged();
    void resultChatsChanged();
    void countChanged();
    // A page of results has replaced or extended what was here.
    void resultsArrived();

private:
    void issue(bool append);
    void cancel();
    void clearInflight();
    void reset();      // drop the results and everything describing them
    void clearRows();  // the results alone
    void forgetPage(); // everything describing them alone
    void askRemoteSupport();
    void setComplete(bool v);
    void setRemoteAvailable(bool v);
    void rebuildResultChats();

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_chat;
    QString m_query;   // what is typed
    QString m_matched; // what the last search went out with, held for paging
    QString m_cursor;  // `last`, resent verbatim: account-wide it is a pair
    QString m_tag;     // ours alone, so one window's cancel spares the others
    bool m_alsoRemote = false;
    bool m_remoteAvailable = false;
    bool m_complete = false;
    bool m_searched = false;
    bool m_failed = false;
    // Whether the in-flight request pages behind the displayed results or
    // replaces them. Decided when it goes out, not when it comes back.
    bool m_appending = false;
    int m_token = 0;     // in-flight search, 0 for none
    int m_capsToken = 0; // in-flight mam fulltextSupported

    QList<QVariantMap> m_msgs;
    QStringList m_resultChats;
};

#endif // SEARCHMODEL_H
