/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_desktop_model.h"

#include "win/singleton_interface.h"
#include "win/space.h"
#include "win/virtual_desktops.h"

namespace KWin::scripting::models::v3
{

virtual_desktop_model::virtual_desktop_model(QObject* parent)
    : QAbstractListModel(parent)
{
    auto vds = win::singleton_interface::virtual_desktops->qobject;
    connect(vds,
            &win::virtual_desktop_manager_qobject::desktopCreated,
            this,
            &virtual_desktop_model::handleVirtualDesktopAdded);
    connect(vds,
            &win::virtual_desktop_manager_qobject::desktopRemoved,
            this,
            &virtual_desktop_model::handleVirtualDesktopRemoved);

    m_virtualDesktops = win::singleton_interface::virtual_desktops->get();
}

void virtual_desktop_model::create(uint position, const QString& name)
{
    win::singleton_interface::virtual_desktops->create(position, name);
}

void virtual_desktop_model::remove(uint position)
{
    if (static_cast<int>(position) < m_virtualDesktops.count()) {
        win::singleton_interface::virtual_desktops->remove(m_virtualDesktops[position]->id());
    }
}

void virtual_desktop_model::handleVirtualDesktopAdded(win::virtual_desktop* desktop)
{
    const int position = desktop->x11DesktopNumber() - 1;
    beginInsertRows(QModelIndex(), position, position);
    m_virtualDesktops.insert(position, desktop);
    endInsertRows();
}

void virtual_desktop_model::handleVirtualDesktopRemoved(win::virtual_desktop* desktop)
{
    const int index = m_virtualDesktops.indexOf(desktop);
    Q_ASSERT(index != -1);

    beginRemoveRows(QModelIndex(), index, index);
    m_virtualDesktops.removeAt(index);
    endRemoveRows();
}

QHash<int, QByteArray> virtual_desktop_model::roleNames() const
{
    QHash<int, QByteArray> roleNames = QAbstractListModel::roleNames();
    roleNames.insert(DesktopRole, QByteArrayLiteral("desktop"));
    return roleNames;
}

win::virtual_desktop* virtual_desktop_model::desktopFromIndex(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_virtualDesktops.count()) {
        return nullptr;
    }
    return m_virtualDesktops[index.row()];
}

QVariant virtual_desktop_model::data(const QModelIndex& index, int role) const
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

int virtual_desktop_model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_virtualDesktops.count();
}

} // namespace KWin::ScriptingModels::V3
