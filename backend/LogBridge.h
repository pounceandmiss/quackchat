// Qt's own messages, forwarded into tacky's log so one file holds both halves
// of a session: a warning from a QML binding next to the stanza that provoked
// it.
//
// The handler already installed stays in the chain, so a terminal goes on
// seeing everything. What the file gets is whatever QLoggingCategory let
// through, filtered again by the backend's own level - two thresholds in
// series, and the quieter one wins.
#ifndef LOGBRIDGE_H
#define LOGBRIDGE_H

#include <QVariantMap>
#include <QtGlobal>

class QMessageLogContext;
class TackyBackend;

// The `log write` arguments for one Qt message. Empty for a message that must
// not be forwarded, which is our own wire logging: sending it would log the
// send.
QVariantMap logWriteArgs(QtMsgType type, const QMessageLogContext &ctx,
                         const QString &msg);

// Install the handler, from main() and before anything else logs; `backend` has
// to outlive the application. Exactly once: a second call would chain the
// handler to itself. Messages logged before the backend is up are not held for
// it - they reach the previous handler and no further.
void installLogBridge(TackyBackend *backend);

// Whether to forward at all. Off until the backend has a log file: with the
// sink still on stderr the previous handler has already printed the message,
// and forwarding would only print it a second time.
void setLogBridgeActive(bool active);

#endif // LOGBRIDGE_H
