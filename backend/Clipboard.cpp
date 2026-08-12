#include "Clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

void Clipboard::setText(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}
