#include "MapListModel.h"

int MapListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MapListModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_items.size())
        return {};
    const QVariantMap &e = m_items.at(index.row());
    if (role == RawRole)
        return e;
    const int i = role - (Qt::UserRole + 1);
    if (i < 0 || i >= m_keys.size())
        return {};
    return e.value(m_keys.at(i));
}

QHash<int, QByteArray> MapListModel::roleNames() const {
    QHash<int, QByteArray> r;
    for (int i = 0; i < m_keys.size(); ++i)
        r.insert(Qt::UserRole + 1 + i, m_keys.at(i).toUtf8());
    r.insert(RawRole, "raw");
    return r;
}
