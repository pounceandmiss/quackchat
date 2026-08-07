// The publish-side image pipeline: whatever the picker returned comes back as
// a square PNG of exactly kEdge, cropped from the middle. These bytes are the
// blob every subscriber downloads, so the size and the framing are the point.
#include <QtTest>

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "QImageAvatarEncoder.h"

class TestAvatarEncoder : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    // Three vertical stripes, so which third survived the crop is readable off
    // a single pixel.
    static QImage stripes(int w, int h) {
        QImage img(w, h, QImage::Format_RGB32);
        { // the painter has to be gone before the image is handed back
            QPainter p(&img);
            p.fillRect(0, 0, w / 3, h, Qt::red);
            p.fillRect(w / 3, 0, w - 2 * (w / 3), h, Qt::green);
            p.fillRect(w - w / 3, 0, w / 3, h, Qt::blue);
        }
        return img;
    }

    QString write(const QImage &img, const QString &name) {
        const QString path = dir.filePath(name);
        return img.save(path) ? path : QString();
    }

    static QImage decode(const QByteArray &png) {
        QImage out;
        out.loadFromData(png, "PNG");
        return out;
    }

private slots:
    void cropsFromTheCentreAndScales() {
        const QString path = write(stripes(300, 100), "wide.png");
        QVERIFY(!path.isEmpty());

        QString error = QStringLiteral("untouched");
        const AvatarImage a = QImageAvatarEncoder().encode(QUrl::fromLocalFile(path),
                                                           &error);
        QVERIFY2(!a.png.isEmpty(), qPrintable(error));
        QCOMPARE(error, QString("untouched")); // left alone on success
        QCOMPARE(a.width, QImageAvatarEncoder::kEdge);
        QCOMPARE(a.height, QImageAvatarEncoder::kEdge);
        QVERIFY(a.png.startsWith("\x89PNG")); // really PNG, not just bytes

        const QImage out = decode(a.png);
        QCOMPARE(out.size(), QSize(QImageAvatarEncoder::kEdge,
                                   QImageAvatarEncoder::kEdge));
        // A 300x100 crop is the middle 100px: all green, no red or blue edge.
        QCOMPARE(out.pixelColor(4, 64), QColor(Qt::green));
        QCOMPARE(out.pixelColor(64, 64), QColor(Qt::green));
        QCOMPARE(out.pixelColor(QImageAvatarEncoder::kEdge - 5, 64),
                 QColor(Qt::green));
    }

    // Portrait crops the same way, and a picture smaller than kEdge is scaled
    // up rather than published at its own size.
    void tallAndSmallBothLandOnKEdge() {
        const QString tall = write(stripes(100, 300), "tall.png");
        const AvatarImage a = QImageAvatarEncoder().encode(QUrl::fromLocalFile(tall),
                                                           nullptr);
        QVERIFY(!a.png.isEmpty());
        QCOMPARE(decode(a.png).size(), QSize(QImageAvatarEncoder::kEdge,
                                             QImageAvatarEncoder::kEdge));

        const QString small = write(stripes(40, 60), "small.png");
        const AvatarImage b = QImageAvatarEncoder().encode(QUrl::fromLocalFile(small),
                                                           nullptr);
        QVERIFY(!b.png.isEmpty());
        QCOMPARE(b.width, QImageAvatarEncoder::kEdge);
        QCOMPARE(decode(b.png).size(), QSize(QImageAvatarEncoder::kEdge,
                                             QImageAvatarEncoder::kEdge));
    }

    // What the Android picker can hand over, since it cannot filter by type.
    void rejectsWhatIsNotAnImage() {
        const QString path = dir.filePath("notes.txt");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a png");
        f.close();

        QString error;
        const AvatarImage a = QImageAvatarEncoder().encode(QUrl::fromLocalFile(path),
                                                           &error);
        QVERIFY(a.png.isEmpty());
        QVERIFY(!error.isEmpty());

        // A file that is not there at all fails the same way, not by crashing.
        QString missingError;
        const AvatarImage missing = QImageAvatarEncoder().encode(
            QUrl::fromLocalFile(dir.filePath("gone.png")), &missingError);
        QVERIFY(missing.png.isEmpty());
        QVERIFY(!missingError.isEmpty());

        // And a caller that does not want the reason is allowed to skip it.
        QVERIFY(QImageAvatarEncoder().encode(QUrl(), nullptr).png.isEmpty());
    }
};

QTEST_MAIN(TestAvatarEncoder)
#include "tst_avatarencoder.moc"
