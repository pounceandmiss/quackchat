// The system clipboard. QML has no type for it, and the way round that is to
// hide a TextEdit and borrow its copy() - which every page that copies anything
// was doing for itself. This is the one QGuiApplication::clipboard() they share.
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class Clipboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Clipboard(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void setText(const QString &text);
};

#endif // CLIPBOARD_H
