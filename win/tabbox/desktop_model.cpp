/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
// own
#include "desktop_model.h"
// tabbox
#include "client_model.h"
#include "tabbox_config.h"
#include "tabbox_handler.h"

#include <cmath>

namespace KWin
{
namespace win
{

DesktopModel::DesktopModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

DesktopModel::~DesktopModel()
{
}

QVariant DesktopModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0)
        return QVariant();

    if (index.parent().isValid()) {
        // parent is valid -> access to Client
        ClientModel* model = m_client_models[m_desktop_list[index.internalId() - 1]];
        return model->data(model->index(index.row(), 0), role);
    }

    const int desktop_index = index.row();
    if (desktop_index >= m_desktop_list.count())
        return QVariant();
    switch (role) {
    case Qt::DisplayRole:
    case DesktopNameRole:
        return tabBox->desktop_name(m_desktop_list[desktop_index]);
    case DesktopRole:
        return m_desktop_list[desktop_index];
    case ClientModelRole:
        return QVariant::fromValue<void*>(m_client_models[m_desktop_list[desktop_index]]);
    default:
        return QVariant();
    }
}

QString DesktopModel::longest_caption() const
{
    QString caption;
    for (int desktop : m_desktop_list) {
        QString desktop_name = tabBox->desktop_name(desktop);
        if (desktop_name.size() > caption.size()) {
            caption = desktop_name;
        }
    }
    return caption;
}

int DesktopModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int DesktopModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        if (parent.internalId() != 0 || parent.row() >= m_desktop_list.count()) {
            return 0;
        }
        const int desktop = m_desktop_list.at(parent.row());
        const ClientModel* model = m_client_models.value(desktop);
        return model->rowCount();
    }
    return m_desktop_list.count();
}

QModelIndex DesktopModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() == 0) {
        return QModelIndex();
    }
    const int row = child.internalId() - 1;
    if (row >= m_desktop_list.count()) {
        return QModelIndex();
    }
    return createIndex(row, 0);
}

QModelIndex DesktopModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0) {
        return QModelIndex();
    }
    if (row < 0) {
        return QModelIndex();
    }
    if (parent.isValid()) {
        if (parent.row() < 0 || parent.row() >= m_desktop_list.count()
            || parent.internalId() != 0) {
            return QModelIndex();
        }
        const int desktop = m_desktop_list.at(parent.row());
        const ClientModel* model = m_client_models.value(desktop);
        if (row >= model->rowCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, parent.row() + 1);
    }
    if (row > m_desktop_list.count() || m_desktop_list.isEmpty())
        return QModelIndex();
    return createIndex(row, column);
}

QHash<int, QByteArray> DesktopModel::roleNames() const
{
    return {
        {Qt::DisplayRole, QByteArrayLiteral("display")},
        {DesktopNameRole, QByteArrayLiteral("caption")},
        {DesktopRole, QByteArrayLiteral("desktop")},
        {ClientModelRole, QByteArrayLiteral("client")},
    };
}

QModelIndex DesktopModel::desktop_index(int desktop) const
{
    if (desktop > m_desktop_list.count())
        return QModelIndex();
    return createIndex(m_desktop_list.indexOf(desktop), 0);
}

void DesktopModel::create_desktop_list()
{
    beginResetModel();
    m_desktop_list.clear();
    qDeleteAll(m_client_models);
    m_client_models.clear();

    switch (tabBox->config().desktop_switching_mode()) {
    case TabBoxConfig::MostRecentlyUsedDesktopSwitching: {
        int desktop = tabBox->current_desktop();
        do {
            m_desktop_list.append(desktop);
            auto* client_model = new ClientModel(this);
            client_model->create_client_list(desktop);
            m_client_models.insert(desktop, client_model);
            desktop = tabBox->next_desktop_focus_chain(desktop);
        } while (desktop != tabBox->current_desktop());
        break;
    }
    case TabBoxConfig::StaticDesktopSwitching: {
        for (int i = 1; i <= tabBox->number_of_desktops(); i++) {
            m_desktop_list.append(i);
            auto* client_model = new ClientModel(this);
            client_model->create_client_list(i);
            m_client_models.insert(i, client_model);
        }
        break;
    }
    }
    endResetModel();
}

} // namespace win
} // namespace KWin
