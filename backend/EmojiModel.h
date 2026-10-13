// The emoji picker's grid, as rows a ListView can draw: a header row per
// group, then the group's emoji `columns` to a row, with what was used last
// on top. Typing into `filter` swaps all of that for the matches alone.
//
// The catalogue is Unicode's (emoji/emoji.tsv, see tools/emoji-gen.py), less
// whatever the emoji font cannot draw: a newer emoji on an older font would
// otherwise sit in the grid as a blank box, or a flag as two letters.
//
// What was used and which skin tones were picked are kept in App.settings, so
// the picker in every window opens on the same recents.
#ifndef EMOJIMODEL_H
#define EMOJIMODEL_H

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class AppSettings;

class EmojiModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    // How many emoji a row holds, which the view works out from its width.
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    // Words to match against names and CLDR's keywords; "" shows everything.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Where recents and tone picks are kept. Null leaves the picker without
    // either, which is how the tests that do not care about them run it.
    Q_PROPERTY(AppSettings *settings READ settings WRITE setSettings
                   NOTIFY settingsChanged)
    // The groups in the rows as they stand, each {section, row}: the key a
    // header carries and the row it is on, for tabs to jump to and to follow
    // the scroll by. Empty while filtering, which has no groups.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)

public:
    enum Roles {
        HeaderRole = Qt::UserRole + 1, // bool: a group's title, not emoji
        SectionRole,                   // the group key, e.g. "smileys"
        // The row's emoji, each {emoji, base, name, toned}: emoji is what to
        // draw and send, the tone picked for it last if any; base is the
        // untoned form the tone picker hangs off; toned says it has tones.
        CellsRole,
    };

    explicit EmojiModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int columns() const { return m_columns; }
    void setColumns(int columns);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    AppSettings *settings() const { return m_settings; }
    void setSettings(AppSettings *settings);
    QVariantList sections() const { return m_sections; }

    // Every tone of `base` the font can draw: five, light to dark, or for a
    // pair a 5x5 square with a row per first person's tone. Empty for an
    // emoji without tones.
    Q_INVOKABLE QStringList variants(const QString &base) const;
    // The best match for `filter`, as the grid draws it; "" when not
    // filtering or nothing matches.
    Q_INVOKABLE QString firstMatch() const;

    // `emoji` was sent: it goes to the front of the recents.
    Q_INVOKABLE void use(const QString &emoji);
    // A tone was picked for `base` (or `base` itself, which drops the pick),
    // and is what the grid shows for it from now on. Also a use.
    Q_INVOKABLE void useTone(const QString &base, const QString &emoji);

    // Builds the catalogue, if nothing has yet. Safe off the UI thread, which
    // is where the app calls it: the first model otherwise waits on it.
    static void preload();

    // The most recents kept; the grid shows two rows' worth of them.
    static constexpr int kRecentKept = 40;

signals:
    void columnsChanged();
    void filterChanged();
    void settingsChanged();
    void sectionsChanged();

private:
    struct Row {
        QString section;
        bool header = false;
        // Catalogue indices, and for recents the exact form that was used
        // (a tone of the entry, say), in step with them.
        QVector<int> entries;
        QStringList shown;
    };

    void rebuild();
    QVariantMap cell(int entry, const QString &shown) const;
    QString preferred(int entry) const;
    QVector<int> matches() const;

    int m_columns = 8;
    QString m_filter;
    QPointer<AppSettings> m_settings;
    QVector<Row> m_rows;
    QVariantList m_sections;
};

#endif // EMOJIMODEL_H
