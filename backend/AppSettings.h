// The preferences that belong to the app rather than to an account, exposed to
// QML as `App.settings`. tacky keeps them in one global key/value store; this
// is the slice of it the GUI has anything to say about.
//
// Every value is a string and an unset key reads as an empty one, so tacky's
// own defaults are repeated here, as they are in the Tk client's setting menu
// (`variable autofetchVar "contacts"`). A control showing what is in force
// cannot show "" for a setting quietly behaving as `contacts`. They are tacky's
// to change, so each one names where it comes from: this is `file.tcl`'s
// AUTOFETCH_DEFAULT.
#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class AppSettings : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // everyone | contacts | never: whose inline images load unasked.
    Q_PROPERTY(QString attachmentAutofetch READ attachmentAutofetch
                   NOTIFY attachmentAutofetchChanged)
    // And how big one may be, in bytes; 0 is no cap.
    Q_PROPERTY(qlonglong attachmentAutofetchMax READ attachmentAutofetchMax
                   NOTIFY attachmentAutofetchMaxChanged)
    // Whether the backend writes its log to a file. The same key the Tk
    // client's File menu toggles, so the two agree about one session's store.
    Q_PROPERTY(bool logToFile READ logToFile NOTIFY logToFileChanged)
    // How much of it: verbose, debug, info, warning, error or none.
    Q_PROPERTY(QString logLevel READ logLevel NOTIFY logLevelChanged)
    // The native loggers inside libdatachannel and rtc-ma. They filter their
    // own output and are voluminous with it, so this is a switch rather than a
    // level of its own.
    Q_PROPERTY(bool logNative READ logNative NOTIFY logNativeChanged)
    // Whether message text is blanked out of it. Best effort, and on unless
    // turned off; the same key as the Tk client's.
    Q_PROPERTY(bool logRedact READ logRedact NOTIFY logRedactChanged)
    // Whether the chat feed draws a face beside each run of messages. A key of
    // our own: the Tk client draws them either way.
    Q_PROPERTY(bool chatAvatars READ chatAvatars NOTIFY chatAvatarsChanged)
    // full | compact | columns: the conversations list's rows. compact is one
    // line per chat, and columns puts two of those side by side. A key of our
    // own.
    Q_PROPERTY(QString chatListStyle READ chatListStyle NOTIFY chatListStyleChanged)
    // Whether a connection is checked after the machine wakes or the app comes
    // back, dropping one that has quietly died. On unless turned off: tacky's
    // conn_probe.
    Q_PROPERTY(bool connProbe READ connProbe NOTIFY connProbeChanged)
    // Whether contacts may ask the local time (XEP-0202) and how long the app
    // has been idle (XEP-0012). Off unless turned on, and answered only to
    // contacts with a subscription to us.
    Q_PROPERTY(bool answerTime READ answerTime NOTIFY answerTimeChanged)
    Q_PROPERTY(bool answerLastActivity READ answerLastActivity
                   NOTIFY answerLastActivityChanged)
    // "" (tacky picks) | rtc | webrtc, stored by tacky, which reads it when it
    // picks a backend. Takes effect at the next start.
    Q_PROPERTY(QString mediaBackend READ mediaBackend NOTIFY mediaBackendChanged)
    // What the setting above actually produced this run, which is not the same
    // thing: a backend that would not load leaves rtc running. "" until the
    // backend answers.
    Q_PROPERTY(QString activeMediaBackend READ activeMediaBackend
                   NOTIFY activeMediaBackendChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString attachmentAutofetch() const { return m_autofetch; }
    qlonglong attachmentAutofetchMax() const { return m_autofetchMax; }
    bool logToFile() const { return m_logToFile; }
    QString logLevel() const { return m_logLevel; }
    bool logNative() const { return m_logNative; }
    bool logRedact() const { return m_logRedact; }
    bool chatAvatars() const { return m_chatAvatars; }
    QString chatListStyle() const { return m_chatListStyle; }
    bool connProbe() const { return m_connProbe; }
    bool answerTime() const { return m_answerTime; }
    bool answerLastActivity() const { return m_answerLastActivity; }
    QString mediaBackend() const { return m_mediaBackend; }
    QString activeMediaBackend() const { return m_activeMediaBackend; }

    void setBackend(TackyBackend *backend);

    // Nothing announces them at startup, so whoever shows them asks once.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void setAttachmentAutofetch(const QString &policy);
    Q_INVOKABLE void setAttachmentAutofetchMax(qlonglong bytes);
    Q_INVOKABLE void setLogToFile(bool on);
    Q_INVOKABLE void setLogLevel(const QString &level);
    Q_INVOKABLE void setLogNative(bool on);
    Q_INVOKABLE void setLogRedact(bool on);
    Q_INVOKABLE void setChatAvatars(bool on);
    // Unknown styles are ignored.
    Q_INVOKABLE void setChatListStyle(const QString &style);
    Q_INVOKABLE void setConnProbe(bool on);
    Q_INVOKABLE void setAnswerTime(bool on);
    Q_INVOKABLE void setAnswerLastActivity(bool on);
    // Unknown names are ignored.
    Q_INVOKABLE void setMediaBackend(const QString &name);

    // Public so tests can drive them with canned events and replies.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void attachmentAutofetchChanged();
    void attachmentAutofetchMaxChanged();
    void logToFileChanged();
    void logLevelChanged();
    void logNativeChanged();
    void logRedactChanged();
    void chatAvatarsChanged();
    void chatListStyleChanged();
    void connProbeChanged();
    void answerTimeChanged();
    void answerLastActivityChanged();
    void mediaBackendChanged();
    void activeMediaBackendChanged();

private:
    void applyValue(const QString &key, const QString &value);
    void write(const QString &key, const QString &value);

    TackyBackend *m_backend = nullptr;
    // tacky's own defaults, in force until a stored value replaces them.
    QString m_autofetch = QStringLiteral("contacts");
    qlonglong m_autofetchMax = 5242880;
    bool m_logToFile = false;
    QString m_logLevel = QStringLiteral("warning");
    bool m_logNative = false;
    bool m_logRedact = true;
    bool m_chatAvatars = true;
    QString m_chatListStyle = QStringLiteral("full");
    bool m_connProbe = true;
    bool m_answerTime = false;
    bool m_answerLastActivity = false;
    QString m_mediaBackend;   // "" until stored: the automatic choice
    QString m_activeMediaBackend;

    int m_autofetchToken = -1;
    int m_autofetchMaxToken = -1;
    int m_logToFileToken = -1;
    int m_logLevelToken = -1;
    int m_logNativeToken = -1;
    int m_logRedactToken = -1;
    int m_chatAvatarsToken = -1;
    int m_chatListStyleToken = -1;
    int m_connProbeToken = -1;
    int m_answerTimeToken = -1;
    int m_answerLastActivityToken = -1;
    int m_mediaBackendToken = -1;
    int m_activeBackendToken = -1;
};

#endif // APPSETTINGS_H
