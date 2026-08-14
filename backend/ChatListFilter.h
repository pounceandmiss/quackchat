// The per-view slice of a ChatListModel: what the filter box matches, in the
// order the view was asked for. It is a proxy rather than options on the model
// because every window on an account shares one ChatListModel - typing in one
// list must not reorder another.
//
// Matching is the Tk list's: a case-insensitive substring of the JID or of the
// name, with an empty query matching everything.
#ifndef CHATLISTFILTER_H
#define CHATLISTFILTER_H

#include <QList>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

class ChatListFilter : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    // Rows the filter is hiding. The difference between "no conversations" and
    // "none matching what you typed", which the two need different words for.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    // Rows that survived it. A view has a count of its own, but that one is an
    // answer given mid-layout: anything sized off it that the view then has to
    // lay out - a heading over the rows, say - asks the question again while
    // the first answer is still being acted on, which is a binding loop.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum SortMode {
        Recent, // whatever order the source is in, which is by last activity
        Name,
    };
    Q_ENUM(SortMode)

    explicit ChatListFilter(QObject *parent = nullptr);

    QAbstractItemModel *source() const { return sourceModel(); }
    QString query() const { return m_query; }
    SortMode sortMode() const { return m_sortMode; }
    int totalCount() const;
    int count() const { return rowCount(); }

    void setSource(QAbstractItemModel *model);
    void setQuery(const QString &query);
    void setSortMode(SortMode mode);

signals:
    void sourceChanged();
    void queryChanged();
    void sortModeChanged();
    void totalCountChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;

private:
    // Under what the row shows: an unnamed chat is listed by its JID, so that is
    // what it sorts and matches under. The source model's rule.
    QString displayName(const QModelIndex &index) const;
    void applySortMode();

    QString m_query;
    SortMode m_sortMode = Recent;
    // Ours alone, so swapping the source leaves the proxy's own intact.
    QList<QMetaObject::Connection> m_sourceCounts;
};

#endif // CHATLISTFILTER_H
