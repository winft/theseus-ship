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
#include "clientmodel.h"
// tabbox
#include "tabboxconfig.h"
#include "tabboxhandler.h"
// Qt
#include <QIcon>
#include <QUuid>
// TODO: remove with Qt 5, only for HTML escaping the caption
#include <QTextDocument>
// other
#include <algorithm>
#include <cmath>

namespace KWin
{
namespace TabBox
{

ClientModel::ClientModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

ClientModel::~ClientModel()
{
}

QVariant ClientModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (m_client_list.empty()) {
        return QVariant();
    }

    auto client_index = index.row();
    if (client_index >= static_cast<int>(m_client_list.size())) {
        return QVariant();
    }

    auto client = m_client_list[client_index].lock();
    if (!client) {
        return QVariant();
    }
    switch (role) {
    case Qt::DisplayRole:
    case CaptionRole: {
        QString caption = client->caption();
        if (Qt::mightBeRichText(caption)) {
            caption = caption.toHtmlEscaped();
        }
        return caption;
    }
    case ClientRole:
        return QVariant::fromValue<void*>(client.get());
    case DesktopNameRole: {
        return tabBox->desktop_name(client.get());
    }
    case WIdRole:
        return client->internal_id();
    case MinimizedRole:
        return client->is_minimized();
    case CloseableRole:
        // clients that claim to be first are not closeable
        return client->is_closeable() && !client->is_first_in_tabbox();
    case IconRole:
        return client->icon();
    default:
        return QVariant();
    }
}

QString ClientModel::longest_caption() const
{
    QString caption;
    for (auto const& client_pointer : m_client_list) {
        auto client = client_pointer.lock();
        if (!client) {
            continue;
        }
        if (client->caption().size() > caption.size()) {
            caption = client->caption();
        }
    }
    return caption;
}

int ClientModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int ClientModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_client_list.size();
}

QModelIndex ClientModel::parent(const QModelIndex& child) const
{
    Q_UNUSED(child)
    return QModelIndex();
}

QModelIndex ClientModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column != 0 || parent.isValid()) {
        return QModelIndex();
    }
    auto index = row * columnCount();
    if (index >= static_cast<int>(m_client_list.size()) && !m_client_list.empty()) {
        return QModelIndex();
    }
    return createIndex(row, 0);
}

QHash<int, QByteArray> ClientModel::roleNames() const
{
    return {
        {CaptionRole, QByteArrayLiteral("caption")},
        {DesktopNameRole, QByteArrayLiteral("desktopName")},
        {MinimizedRole, QByteArrayLiteral("minimized")},
        {WIdRole, QByteArrayLiteral("windowId")},
        {CloseableRole, QByteArrayLiteral("closeable")},
        {IconRole, QByteArrayLiteral("icon")},
    };
}

QModelIndex ClientModel::index(TabBoxClient* client) const
{
    auto it = std::find_if(m_client_list.cbegin(), m_client_list.cend(), [client](auto const& cmp) {
        return client == cmp.lock().get();
    });
    if (it == m_client_list.cend()) {
        return QModelIndex();
    }
    auto index = it - m_client_list.cbegin();
    auto row = index / columnCount();
    auto column = index % columnCount();
    return createIndex(row, column);
}

void ClientModel::create_client_list(bool partial_reset)
{
    create_client_list(tabBox->current_desktop(), partial_reset);
}

void ClientModel::create_client_list(int desktop, bool partial_reset)
{
    auto start = tabBox->active_client().lock().get();
    // TODO: new clients are not added at correct position
    if (partial_reset && !m_client_list.empty()) {
        auto first_client = m_client_list.at(0).lock();
        if (first_client) {
            start = first_client.get();
        }
    }

    beginResetModel();
    m_client_list.clear();
    TabBoxClientList sticky_clients;

    auto remove_clients = [this](auto const& target) {
        m_client_list.erase(std::remove_if(m_client_list.begin(),
                                           m_client_list.end(),
                                           [&target](auto const& client) {
                                               return target.get() == client.lock().get();
                                           }),
                            m_client_list.end());
    };

    switch (tabBox->config().client_switching_mode()) {
    case TabBoxConfig::FocusChainSwitching: {
        TabBoxClient* c = start;
        if (!tabBox->is_in_focus_chain(c)) {
            auto first_client = tabBox->first_client_focus_chain().lock();
            if (first_client) {
                c = first_client.get();
            }
        }
        TabBoxClient* stop = c;
        do {
            auto add = tabBox->client_to_add_to_list(c, desktop).lock();
            if (add) {
                m_client_list.push_back(add);
                if (add.get()->is_first_in_tabbox()) {
                    sticky_clients.push_back(add);
                }
            }
            c = tabBox->next_client_focus_chain(c).lock().get();
        } while (c && c != stop);
        break;
    }
    case TabBoxConfig::StackingOrderSwitching: {
        // TODO: needs improvement
        auto stacking = tabBox->stacking_order();
        auto c = stacking.at(0).lock().get();
        auto stop = c;
        auto index = 0u;
        while (c) {
            auto add_weak = tabBox->client_to_add_to_list(c, desktop);
            if (auto add = add_weak.lock()) {
                if (start == add.get()) {
                    remove_clients(add);
                    m_client_list.push_back(add);
                } else
                    m_client_list.push_back(add);
                if (add->is_first_in_tabbox()) {
                    sticky_clients.push_back(add);
                }
            }
            if (index >= stacking.size() - 1) {
                c = nullptr;
            } else {
                c = stacking[++index].lock().get();
            }

            if (c == stop)
                break;
        }
        break;
    }
    }
    for (auto const& c : sticky_clients) {
        remove_clients(c.lock());
        m_client_list.push_back(c);
    }
    if (tabBox->config().client_applications_mode() != TabBoxConfig::AllWindowsCurrentApplication
        && (tabBox->config().show_desktop_mode() == TabBoxConfig::ShowDesktopClient
            || m_client_list.empty())) {
        auto desktop_client = tabBox->desktop_client().lock();
        if (desktop_client) {
            m_client_list.push_back(desktop_client);
        }
    }
    endResetModel();
}

void ClientModel::close(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    if (auto client = m_client_list.at(i).lock()) {
        client->close();
    }
}

void ClientModel::activate(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    tabBox->set_current_index(ind);
    tabBox->activate_and_close();
}

} // namespace Tabbox
} // namespace KWin
