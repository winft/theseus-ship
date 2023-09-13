/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
// own
#include "tabbox_client_model.h"
// tabbox
#include "tabbox_client.h"
#include "tabbox_config.h"
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
namespace win
{

tabbox_client_model::tabbox_client_model(QObject* parent)
    : QAbstractItemModel(parent)
{
}

tabbox_client_model::~tabbox_client_model()
{
}

QVariant tabbox_client_model::data(const QModelIndex& index, int role) const
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

    auto client = m_client_list[client_index];
    switch (role) {
    case Qt::DisplayRole:
    case CaptionRole: {
        return client->caption();
    }
    case ClientRole:
        return QVariant::fromValue<void*>(client);
    case DesktopNameRole: {
        return tabbox_handle->desktop_name(client);
    }
    case WIdRole:
        return client->internal_id();
    case MinimizedRole:
        return client->is_minimized();
    case CloseableRole:
        return client->is_closeable();
    case IconRole:
        return client->icon();
    default:
        return QVariant();
    }
}

QString tabbox_client_model::longest_caption() const
{
    QString caption;
    for (auto const& client : m_client_list) {
        if (client->caption().size() > caption.size()) {
            caption = client->caption();
        }
    }
    return caption;
}

int tabbox_client_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int tabbox_client_model::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_client_list.size();
}

QModelIndex tabbox_client_model::parent(const QModelIndex& child) const
{
    Q_UNUSED(child)
    return QModelIndex();
}

QModelIndex tabbox_client_model::index(int row, int column, const QModelIndex& parent) const
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

QHash<int, QByteArray> tabbox_client_model::roleNames() const
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

QModelIndex tabbox_client_model::index(tabbox_client* client) const
{
    auto it = std::find_if(m_client_list.cbegin(), m_client_list.cend(), [client](auto const& cmp) {
        return client == cmp;
    });

    if (it == m_client_list.cend()) {
        return QModelIndex();
    }

    auto index = it - m_client_list.cbegin();
    auto row = index / columnCount();
    auto column = index % columnCount();
    return createIndex(row, column);
}

void tabbox_client_model::create_client_list(bool partial_reset)
{
    create_client_list(tabbox_handle->current_desktop(), partial_reset);
}

void tabbox_client_model::create_client_list(int desktop, bool partial_reset)
{
    auto start = tabbox_handle->active_client();

    // TODO: new clients are not added at correct position
    if (partial_reset && !m_client_list.empty()) {
        if (auto first = m_client_list.at(0)) {
            start = first;
        }
    }

    beginResetModel();
    m_client_list.clear();

    auto remove_clients = [this](auto const& target) {
        m_client_list.erase(
            std::remove_if(m_client_list.begin(),
                           m_client_list.end(),
                           [&target](auto const& client) { return target == client; }),
            m_client_list.end());
    };

    switch (tabbox_handle->config().client_switching_mode()) {
    case tabbox_config::FocusChainSwitching: {
        tabbox_client* c = start;
        if (!tabbox_handle->is_in_focus_chain(c)) {
            if (auto first = tabbox_handle->first_client_focus_chain()) {
                c = first;
            }
        }
        tabbox_client* stop = c;
        do {
            if (auto add = tabbox_handle->client_to_add_to_list(c, desktop)) {
                m_client_list.push_back(add);
            }
            c = tabbox_handle->next_client_focus_chain(c);
        } while (c && c != stop);
        break;
    }
    case tabbox_config::StackingOrderSwitching: {
        // TODO: needs improvement
        auto stacking = tabbox_handle->stacking_order();
        auto c = stacking.at(0);
        auto stop = c;
        auto index = 0u;
        while (c) {
            if (auto add = tabbox_handle->client_to_add_to_list(c, desktop)) {
                if (start == add) {
                    remove_clients(add);
                }
                m_client_list.push_back(add);
            }

            if (index >= stacking.size() - 1) {
                c = nullptr;
            } else {
                c = stacking[++index];
            }

            if (c == stop) {
                break;
            }
        }
        break;
    }
    }
    if (tabbox_handle->config().client_applications_mode()
            != tabbox_config::AllWindowsCurrentApplication
        && (tabbox_handle->config().show_desktop_mode() == tabbox_config::ShowDesktopClient
            || m_client_list.empty())) {
        if (auto desktop_client = tabbox_handle->desktop_client()) {
            m_client_list.push_back(desktop_client);
        }
    }
    endResetModel();
}

void tabbox_client_model::close(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    m_client_list.at(i)->close();
}

void tabbox_client_model::activate(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    tabbox_handle->set_current_index(ind);
    tabbox_handle->activate_and_close();
}

} // namespace win
} // namespace KWin
