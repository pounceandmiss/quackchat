#include "ChatListFilter.h"

#include "ChatListModel.h"

ChatListFilter::ChatListFilter(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    // Off our own signals, not the source's: a query changes how many rows are
    // here without changing anything there.
    connect(this, &QAbstractItemModel::rowsInserted, this, &ChatListFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ChatListFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ChatListFilter::countChanged);
}

int ChatListFilter::totalCount() const {
    return sourceModel() ? sourceModel()->rowCount() : 0;
}

void ChatListFilter::setSource(QAbstractItemModel *model) {
    if (sourceModel() == model)
        return;
    // Dropped one at a time rather than with a blanket disconnect from the old
    // model, which would take the proxy's own connections to it down as well.
    for (const QMetaObject::Connection &c : std::as_const(m_sourceCounts))
        disconnect(c);
    m_sourceCounts.clear();
    setSourceModel(model);
    if (model) {
        // rowsInserted/Removed alone miss the reset a full reload does.
        m_sourceCounts << connect(model, &QAbstractItemModel::rowsInserted,
                                  this, &ChatListFilter::totalCountChanged)
                       << connect(model, &QAbstractItemModel::rowsRemoved,
                                  this, &ChatListFilter::totalCountChanged)
                       << connect(model, &QAbstractItemModel::modelReset,
                                  this, &ChatListFilter::totalCountChanged);
    }
    applySortMode(); // setSourceModel leaves the new one unsorted
    emit sourceChanged();
    emit totalCountChanged();
    emit countChanged();
}

void ChatListFilter::setQuery(const QString &query) {
    if (m_query == query)
        return;
    beginFilterChange();
    m_query = query;
    endFilterChange();
    emit queryChanged();
}

void ChatListFilter::setSortMode(SortMode mode) {
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    applySortMode();
    emit sortModeChanged();
}

// Recent is the source's own order, so it is expressed by not sorting at all
// rather than by a comparison that would have to repeat the model's rule.
void ChatListFilter::applySortMode() {
    if (m_sortMode == Name)
        sort(0, Qt::AscendingOrder);
    else
        sort(-1);
}

QString ChatListFilter::displayName(const QModelIndex &index) const {
    const QString name = index.data(ChatListModel::NameRole).toString();
    return name.isEmpty() ? index.data(ChatListModel::JidRole).toString() : name;
}

bool ChatListFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (m_query.isEmpty() || !sourceModel())
        return true;
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (i.data(ChatListModel::JidRole).toString().contains(m_query, Qt::CaseInsensitive))
        return true;
    return i.data(ChatListModel::NameRole).toString().contains(m_query,
                                                               Qt::CaseInsensitive);
}

// Name mode only; Recent never sorts. JID breaks the tie so the order is total
// and stable, as it is in the model.
bool ChatListFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const {
    const int c = displayName(a).compare(displayName(b), Qt::CaseInsensitive);
    if (c != 0)
        return c < 0;
    return a.data(ChatListModel::JidRole).toString()
         < b.data(ChatListModel::JidRole).toString();
}

int ChatListFilter::rowOfJid(const QString &jid) const {
    for (int r = 0, n = rowCount(); r < n; ++r)
        if (index(r, 0).data(ChatListModel::JidRole).toString() == jid)
            return r;
    return -1;
}

// -1 when there is nowhere to go. A chat the query hides is no place in this
// list to count from, so it counts as none open and steps in from the near end.
int ChatListFilter::stepRow(const QString &fromJid, int delta) const {
    const int n = rowCount();
    if (n == 0)
        return -1;
    const int from = rowOfJid(fromJid);
    if (from < 0)
        return delta >= 0 ? 0 : n - 1;
    return ((from + delta) % n + n) % n; // % keeps the sign of its left side
}

QVariantMap ChatListFilter::entryAt(int row) const {
    if (row < 0 || row >= rowCount())
        return {};
    return index(row, 0).data(MapListModel::RawRole).toMap();
}
