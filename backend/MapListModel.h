// Rows that are tacky's own entries, handed over as they arrived. tacky sends
// each one as a map keyed by name and the QML role name is that same key, so a
// model over them is little more than its list of keys: kKeys[i] is
// Qt::UserRole+1+i, which is what a subclass's Role enum has to line up with.
// RawRole is the whole map, for a view that wants more of an entry than one
// field at a time.
//
// Subclasses fill m_items and own everything about how - what they ask the
// backend for, what order they keep, what they do with an event.
#ifndef MAPLISTMODEL_H
#define MAPLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class MapListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    // Below the keyed roles, which start at Qt::UserRole + 1.
    enum { RawRole = Qt::UserRole };

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

protected:
    explicit MapListModel(QStringList keys, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_keys(std::move(keys)) {}

    QList<QVariantMap> m_items;

private:
    const QStringList m_keys;
};

#endif // MAPLISTMODEL_H
