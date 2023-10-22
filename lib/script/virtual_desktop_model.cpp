/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_desktop_model.h"

#include "win/singleton_interface.h"
#include <win/subspace_manager_qobject.h>

namespace KWin::scripting
{

subspace_model::subspace_model(QObject* parent)
    : QAbstractListModel(parent)
{
    auto vds = win::singleton_interface::subspaces->qobject;
    connect(vds,
            &win::subspace_manager_qobject::subspace_created,
            this,
            &subspace_model::handleVirtualDesktopAdded);
    connect(vds,
            &win::subspace_manager_qobject::subspace_removed,
            this,
            &subspace_model::handleVirtualDesktopRemoved);

    m_virtualDesktops = win::singleton_interface::subspaces->get();
}

win::subspace* subspace_model::create(uint position, const QString& name)
{
    return win::singleton_interface::subspaces->create(position, name);
}

void subspace_model::remove(uint position)
{
    if (position < m_virtualDesktops.size()) {
        win::singleton_interface::subspaces->remove(m_virtualDesktops[position]->id());
    }
}

void subspace_model::handleVirtualDesktopAdded(win::subspace* desktop)
{
    const int position = desktop->x11DesktopNumber() - 1;
    beginInsertRows(QModelIndex(), position, position);
    m_virtualDesktops.insert(m_virtualDesktops.begin() + position, desktop);
    endInsertRows();
}

void subspace_model::handleVirtualDesktopRemoved(win::subspace* desktop)
{
    auto const it = std::ranges::find(m_virtualDesktops, desktop);
    assert(it != m_virtualDesktops.end());

    auto const index = it - m_virtualDesktops.begin();
    beginRemoveRows(QModelIndex(), index, index);
    m_virtualDesktops.erase(it);
    endRemoveRows();
}

QHash<int, QByteArray> subspace_model::roleNames() const
{
    QHash<int, QByteArray> roleNames = QAbstractListModel::roleNames();
    roleNames.insert(DesktopRole, QByteArrayLiteral("desktop"));
    return roleNames;
}

win::subspace* subspace_model::desktopFromIndex(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_virtualDesktops.size())) {
        return nullptr;
    }
    return m_virtualDesktops[index.row()];
}

QVariant subspace_model::data(const QModelIndex& index, int role) const
{
    auto desktop = desktopFromIndex(index);
    if (!desktop) {
        return QVariant();
    }
    switch (role) {
    case Qt::DisplayRole:
    case DesktopRole:
        return QVariant::fromValue(desktop);
    default:
        return QVariant();
    }
}

int subspace_model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_virtualDesktops.size();
}

} // namespace KWin::ScriptingModels::V3
