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

    if (m_clientList.empty()) {
        return QVariant();
    }

    int clientIndex = index.row();
    if (clientIndex >= static_cast<int>(m_clientList.size())) {
        return QVariant();
    }

    auto client = m_clientList[clientIndex].lock();
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
        return tabBox->desktopName(client.get());
    }
    case WIdRole:
        return client->internalId();
    case MinimizedRole:
        return client->isMinimized();
    case CloseableRole:
        // clients that claim to be first are not closeable
        return client->isCloseable() && !client->isFirstInTabBox();
    case IconRole:
        return client->icon();
    default:
        return QVariant();
    }
}

QString ClientModel::longestCaption() const
{
    QString caption;
    for (auto const& clientPointer : m_clientList) {
        auto client = clientPointer.lock();
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
    return m_clientList.size();
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
    int index = row * columnCount();
    if (index >= static_cast<int>(m_clientList.size()) && !m_clientList.empty()) {
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
    auto it = std::find_if(m_clientList.cbegin(), m_clientList.cend(), [client](auto const& cmp) {
        return client == cmp.lock().get();
    });
    if (it == m_clientList.cend()) {
        return QModelIndex();
    }
    int index = it - m_clientList.cbegin();
    int row = index / columnCount();
    int column = index % columnCount();
    return createIndex(row, column);
}

void ClientModel::createClientList(bool partialReset)
{
    createClientList(tabBox->currentDesktop(), partialReset);
}

void ClientModel::createClientList(int desktop, bool partialReset)
{
    auto start = tabBox->activeClient().lock().get();
    // TODO: new clients are not added at correct position
    if (partialReset && !m_clientList.empty()) {
        auto firstClient = m_clientList.at(0).lock();
        if (firstClient) {
            start = firstClient.get();
        }
    }

    beginResetModel();
    m_clientList.clear();
    TabBoxClientList stickyClients;

    auto remove_clients = [this](auto const& target) {
        m_clientList.erase(std::remove_if(m_clientList.begin(),
                                          m_clientList.end(),
                                          [&target](auto const& client) {
                                              return target.get() == client.lock().get();
                                          }),
                           m_clientList.end());
    };

    switch (tabBox->config().clientSwitchingMode()) {
    case TabBoxConfig::FocusChainSwitching: {
        TabBoxClient* c = start;
        if (!tabBox->isInFocusChain(c)) {
            auto firstClient = tabBox->firstClientFocusChain().lock();
            if (firstClient) {
                c = firstClient.get();
            }
        }
        TabBoxClient* stop = c;
        do {
            auto add = tabBox->clientToAddToList(c, desktop).lock();
            if (add) {
                m_clientList.push_back(add);
                if (add.get()->isFirstInTabBox()) {
                    stickyClients.push_back(add);
                }
            }
            c = tabBox->nextClientFocusChain(c).lock().get();
        } while (c && c != stop);
        break;
    }
    case TabBoxConfig::StackingOrderSwitching: {
        // TODO: needs improvement
        auto stacking = tabBox->stackingOrder();
        auto c = stacking.at(0).lock().get();
        auto stop = c;
        auto index = 0u;
        while (c) {
            auto add_weak = tabBox->clientToAddToList(c, desktop);
            if (auto add = add_weak.lock()) {
                if (start == add.get()) {
                    remove_clients(add);
                    m_clientList.push_back(add);
                } else
                    m_clientList.push_back(add);
                if (add->isFirstInTabBox()) {
                    stickyClients.push_back(add);
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
    for (auto const& c : stickyClients) {
        remove_clients(c.lock());
        m_clientList.push_back(c);
    }
    if (tabBox->config().clientApplicationsMode() != TabBoxConfig::AllWindowsCurrentApplication
        && (tabBox->config().showDesktopMode() == TabBoxConfig::ShowDesktopClient
            || m_clientList.empty())) {
        auto desktopClient = tabBox->desktopClient().lock();
        if (desktopClient) {
            m_clientList.push_back(desktopClient);
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
    if (auto client = m_clientList.at(i).lock()) {
        client->close();
    }
}

void ClientModel::activate(int i)
{
    QModelIndex ind = index(i, 0);
    if (!ind.isValid()) {
        return;
    }
    tabBox->setCurrentIndex(ind);
    tabBox->activateAndClose();
}

} // namespace Tabbox
} // namespace KWin
