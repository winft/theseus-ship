/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "clientmodel.h"

#include "../scripting.h"
#include "../window_wrapper.h"
#include "../workspace_wrapper.h"

#include "screens.h"

namespace KWin::ScriptingModels::V3
{

ClientModel::ClientModel(QObject *parent)
    : QAbstractListModel(parent)
{
    auto ws_wrap = Scripting::self()->workspaceWrapper();

    connect(ws_wrap, &WorkspaceWrapper::clientAdded, this, &ClientModel::handleClientAdded);
    connect(ws_wrap, &WorkspaceWrapper::clientRemoved, this, &ClientModel::handleClientRemoved);

    for (auto window : ws_wrap->windows()) {
        m_clients << window;
        setupClientConnections(window);
    }
}

void ClientModel::markRoleChanged(WindowWrapper *client, int role)
{
    const QModelIndex row = index(m_clients.indexOf(client), 0);
    Q_EMIT dataChanged(row, row, {role});
}

void ClientModel::setupClientConnections(WindowWrapper *client)
{
    connect(client, &WindowWrapper::desktopChanged, this, [this, client]() {
        markRoleChanged(client, DesktopRole);
    });
    connect(client, &WindowWrapper::screenChanged, this, [this, client]() {
        markRoleChanged(client, ScreenRole);
    });
    connect(client, &WindowWrapper::activitiesChanged, this, [this, client]() {
        markRoleChanged(client, ActivityRole);
    });
}

void ClientModel::handleClientAdded(WindowWrapper *client)
{
    beginInsertRows(QModelIndex(), m_clients.count(), m_clients.count());
    m_clients.append(client);
    endInsertRows();

    setupClientConnections(client);
}

void ClientModel::handleClientRemoved(WindowWrapper *client)
{
    const int index = m_clients.indexOf(client);
    Q_ASSERT(index != -1);

    beginRemoveRows(QModelIndex(), index, index);
    m_clients.removeAt(index);
    endRemoveRows();
}

QHash<int, QByteArray> ClientModel::roleNames() const
{
    return {
        { Qt::DisplayRole, QByteArrayLiteral("display") },
        { ClientRole, QByteArrayLiteral("client") },
        { ScreenRole, QByteArrayLiteral("screen") },
        { DesktopRole, QByteArrayLiteral("desktop") },
        { ActivityRole, QByteArrayLiteral("activity") },
    };
}

QVariant ClientModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clients.count()) {
        return QVariant();
    }

    auto client = m_clients[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case ClientRole:
        return QVariant::fromValue(client);
    case ScreenRole:
        return client->screen();
    case DesktopRole:
        return client->desktop();
    case ActivityRole:
        return client->activities();
    default:
        return QVariant();
    }
}

int ClientModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_clients.count();
}

ClientFilterModel::ClientFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

ClientModel *ClientFilterModel::clientModel() const
{
    return m_clientModel;
}

void ClientFilterModel::setClientModel(ClientModel *clientModel)
{
    if (clientModel == m_clientModel) {
        return;
    }
    m_clientModel = clientModel;
    setSourceModel(m_clientModel);
    Q_EMIT clientModelChanged();
}

QString ClientFilterModel::activity() const
{
    return m_activity.value_or(QString());
}

void ClientFilterModel::setActivity(const QString &activity)
{
    if (m_activity != activity) {
        m_activity = activity;
        Q_EMIT activityChanged();
        invalidateFilter();
    }
}

void ClientFilterModel::resetActivity()
{
    if (m_activity.has_value()) {
        m_activity.reset();
        Q_EMIT activityChanged();
        invalidateFilter();
    }
}

int ClientFilterModel::desktop() const
{
    return m_desktop.value_or(0);
}

void ClientFilterModel::setDesktop(int desktop)
{
    if (m_desktop != desktop) {
        m_desktop = desktop;
        Q_EMIT desktopChanged();
        invalidateFilter();
    }
}

void ClientFilterModel::resetDesktop()
{
    if (m_desktop.has_value()) {
        m_desktop.reset();
        Q_EMIT desktopChanged();
        invalidateFilter();
    }
}

QString ClientFilterModel::filter() const
{
    return m_filter;
}

void ClientFilterModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
    invalidateFilter();
}

QString ClientFilterModel::screenName() const
{
    return m_screenName.value_or(QString());
}

void ClientFilterModel::setScreenName(const QString &screen)
{
    if (m_screenName != screen) {
        m_screenName = screen;
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

void ClientFilterModel::resetScreenName()
{
    if (m_screenName.has_value()) {
        m_screenName.reset();
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

ClientFilterModel::WindowTypes ClientFilterModel::windowType() const
{
    return m_windowType.value_or(WindowTypes());
}

void ClientFilterModel::setWindowType(WindowTypes windowType)
{
    if (m_windowType != windowType) {
        m_windowType = windowType;
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

void ClientFilterModel::resetWindowType()
{
    if (m_windowType.has_value()) {
        m_windowType.reset();
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

bool ClientFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (!m_clientModel) {
        return false;
    }
    const QModelIndex index = m_clientModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }
    const QVariant data = index.data();
    if (!data.isValid()) {
        // an invalid QVariant is valid data
        return true;
    }

    auto client = qvariant_cast<WindowWrapper *>(data);
    if (!client) {
        return false;
    }

    if (m_activity.has_value()) {
        if (!client->activities().contains(*m_activity)) {
            return false;
        }
    }

    if (m_desktop.has_value()) {
        if (!client->x11DesktopIds().contains(*m_desktop)) {
            return false;
        }
    }

    if (m_screenName.has_value()) {
        if (screens()->name(client->screen()) != m_screenName) {
            return false;
        }
    }

    if (m_windowType.has_value()) {
        if (!(windowTypeMask(client) & *m_windowType)) {
            return false;
        }
    }

    if (!m_filter.isEmpty()) {
        if (client->caption().contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        const QString windowRole(QString::fromUtf8(client->windowRole()));
        if (windowRole.contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        const QString resourceName(QString::fromUtf8(client->resourceName()));
        if (resourceName.contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        const QString resourceClass(QString::fromUtf8(client->resourceClass()));
        if (resourceClass.contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        return false;
    }
    return true;
}

ClientFilterModel::WindowTypes ClientFilterModel::windowTypeMask(WindowWrapper *client) const
{
    WindowTypes mask;
    if (client->isNormalWindow()) {
        mask |= WindowType::Normal;
    } else if (client->isDialog()) {
        mask |= WindowType::Dialog;
    } else if (client->isDock()) {
        mask |= WindowType::Dock;
    } else if (client->isDesktop()) {
        mask |= WindowType::Desktop;
    } else if (client->isNotification()) {
        mask |= WindowType::Notification;
    } else if (client->isCriticalNotification()) {
        mask |= WindowType::CriticalNotification;
    }
    return mask;
}

} // namespace KWin::ScriptingModels::V3
