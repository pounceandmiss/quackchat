// Display names for the senders in one chat. A message row carries a from_jid,
// never a name; tacky resolves those - participant nick in a room, roster name
// then PEP nick then bare JID in a 1:1 - and this holds the answer for the open
// chat, refreshed by author <Changed> as nicks and roster entries move.
#ifndef AUTHORNAMES_H
#define AUTHORNAMES_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class AuthorNames : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString chat READ chat WRITE setChat NOTIFY chatChanged)
    // from_jid -> name. A property rather than a lookup call so the delegates
    // that read it re-evaluate when a name lands.
    Q_PROPERTY(QVariantMap names READ names NOTIFY namesChanged)

public:
    explicit AuthorNames(QObject *parent = nullptr);

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString chat() const { return m_chat; }
    QVariantMap names() const { return m_names; }
    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setChat(const QString &chat);

    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

signals:
    void backendChanged();
    void accountChanged();
    void chatChanged();
    void namesChanged();

private:
    void refresh();

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_chat;
    QVariantMap m_names;
    int m_pending = 0; // token of the in-flight `author get`, 0 for none
};

#endif // AUTHORNAMES_H
