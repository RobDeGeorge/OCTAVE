// variantlistmodel.h
//
// A list model of QVariantMap rows with a fixed set of roles (one per map
// key), for managers that publish a list a view patches row by row: a field
// change is one dataChanged on one row, so a ListView keeps its delegates and
// its scroll position instead of rebuilding from a reassigned JS array.
// Delegates read `model.<key>`; a key missing from a row reads undefined.

#ifndef VARIANTLISTMODEL_H
#define VARIANTLISTMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>

class VariantListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit VariantListModel(const QStringList &keys, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Row `row` as a map (empty if out of range)
    Q_INVOKABLE QVariantMap get(int row) const;

    void reset(const QList<QVariantMap> &rows);
    void append(const QVariantMap &row);
    void remove(int row);
    // First row whose `key` equals `value`, or -1
    int indexOf(const QString &key, const QVariant &value) const;
    // Set one field; dataChanged for that row and role only when it changed
    void setValue(int row, const QString &key, const QVariant &value);
    // Replace a row's fields; dataChanged for the roles that changed
    void update(int row, const QVariantMap &fields);
    const QList<QVariantMap> &rows() const { return m_rows; }

signals:
    void countChanged();

private:
    QStringList m_keys;
    QList<QVariantMap> m_rows;
};

#endif // VARIANTLISTMODEL_H
