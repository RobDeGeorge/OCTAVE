// variantlistmodel.cpp

#include "variantlistmodel.h"

VariantListModel::VariantListModel(const QStringList &keys, QObject *parent)
    : QAbstractListModel(parent)
    , m_keys(keys)
{
}

int VariantListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant VariantListModel::data(const QModelIndex &index, int role) const
{
    const int key = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_rows.size() || key < 0 || key >= m_keys.size())
        return {};
    return m_rows.at(index.row()).value(m_keys.at(key));
}

QHash<int, QByteArray> VariantListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_keys.size(); ++i)
        names.insert(Qt::UserRole + i, m_keys.at(i).toUtf8());
    return names;
}

QVariantMap VariantListModel::get(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap();
}

void VariantListModel::reset(const QList<QVariantMap> &rows)
{
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = rows;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void VariantListModel::append(const QVariantMap &row)
{
    beginInsertRows(QModelIndex(), int(m_rows.size()), int(m_rows.size()));
    m_rows.append(row);
    endInsertRows();
    emit countChanged();
}

void VariantListModel::remove(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

int VariantListModel::indexOf(const QString &key, const QVariant &value) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(key) == value)
            return i;
    }
    return -1;
}

void VariantListModel::setValue(int row, const QString &key, const QVariant &value)
{
    update(row, {{key, value}});
}

void VariantListModel::update(int row, const QVariantMap &fields)
{
    if (row < 0 || row >= m_rows.size())
        return;
    QVariantMap &r = m_rows[row];
    QList<int> roles;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (r.value(it.key()) == it.value() && r.contains(it.key()))
            continue;
        r.insert(it.key(), it.value());
        const int k = int(m_keys.indexOf(it.key()));
        if (k >= 0)
            roles.append(Qt::UserRole + k);
    }
    if (!roles.isEmpty())
        emit dataChanged(index(row), index(row), roles);
}
