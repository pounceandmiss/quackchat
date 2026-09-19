// The system clipboard. QML has no type for it, and the way round that is to
// hide a TextEdit and borrow its copy() - which every page that copies anything
// was doing for itself. This is the one QGuiApplication::clipboard() they share.
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class Clipboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Clipboard(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void setText(const QString &text);

    // Files copied in a file manager, as far as they are still on disk.
    // Empty for anything else, a copied link included: a url with no local
    // file behind it is text, and the field pastes it as text.
    Q_INVOKABLE QList<QUrl> files();

    // A picture held as pixels rather than as a file - a screenshot, a
    // browser's copy of an image - written out beside the files a dialog
    // picks, so it can be sent the same way. Empty when there is none.
    // Pasting is the only way one reaches the app: a text field takes text.
    Q_INVOKABLE QUrl saveImage();
};

#endif // CLIPBOARD_H
