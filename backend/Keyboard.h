// The on-screen keyboard's height, up from the window's bottom edge, in scene
// units. Zero while it is down, and always on desktop.
//
// On Android it comes from the keyboard's window insets (KeyboardInsets.java).
// Qt.inputMethod.keyboardRectangle is no use there: Qt infers it from the
// window shrinking, which needs adjustResize, and Qt undoes that resize
// whenever a tap re-shows a keyboard that is already up.
//
// Writable so tests can raise a keyboard.
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Keyboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY heightChanged)

public:
    explicit Keyboard(QObject *parent = nullptr);
    ~Keyboard() override;

    qreal height() const { return m_height; }
    void setHeight(qreal height);

signals:
    void heightChanged();

private:
    qreal m_height = 0;
};

#endif // KEYBOARD_H
