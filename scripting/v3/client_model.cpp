/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "client_model.h"

#include "base/singleton_interface.h"
#include "scripting/platform.h"
#include "scripting/singleton_interface.h"
#include "scripting/space.h"
#include "scripting/window.h"

namespace KWin::scripting::models::v3
{

client_model::client_model(QObject* parent)
    : QAbstractListModel(parent)
{
    auto ws_wrap = singleton_interface::qt_script_space;

    connect(ws_wrap, &space::clientAdded, this, &client_model::handleClientAdded);
    connect(ws_wrap, &space::clientRemoved, this, &client_model::handleClientRemoved);

    for (auto window : ws_wrap->windows()) {
        m_clients << window;
        setupClientConnections(window);
    }
}

void client_model::markRoleChanged(window* client, int role)
{
    const QModelIndex row = index(m_clients.indexOf(client), 0);
    Q_EMIT dataChanged(row, row, {role});
}

void client_model::setupClientConnections(window* client)
{
    connect(client, &window::desktopChanged, this, [this, client]() {
        markRoleChanged(client, DesktopRole);
    });
    connect(client, &window::screenChanged, this, [this, client]() {
        markRoleChanged(client, ScreenRole);
    });
}

void client_model::handleClientAdded(window* client)
{
    beginInsertRows(QModelIndex(), m_clients.count(), m_clients.count());
    m_clients.append(client);
    endInsertRows();

    setupClientConnections(client);
}

void client_model::handleClientRemoved(window* client)
{
    const int index = m_clients.indexOf(client);
    Q_ASSERT(index != -1);

    beginRemoveRows(QModelIndex(), index, index);
    m_clients.removeAt(index);
    endRemoveRows();
}

QHash<int, QByteArray> client_model::roleNames() const
{
    return {
        {Qt::DisplayRole, QByteArrayLiteral("display")},
        {ClientRole, QByteArrayLiteral("client")},
        {ScreenRole, QByteArrayLiteral("screen")},
        {DesktopRole, QByteArrayLiteral("desktop")},
        {ActivityRole, QByteArrayLiteral("activity")},
    };
}

QVariant client_model::data(const QModelIndex& index, int role) const
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

int client_model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_clients.count();
}

client_filter_model::client_filter_model(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

client_model* client_filter_model::clientModel() const
{
    return m_clientModel;
}

void client_filter_model::setClientModel(client_model* model)
{
    if (model == m_clientModel) {
        return;
    }
    m_clientModel = model;
    setSourceModel(m_clientModel);
    Q_EMIT clientModelChanged();
}

QString client_filter_model::activity() const
{
    return {};
}

void client_filter_model::setActivity(const QString& /*activity*/)
{
}

void client_filter_model::resetActivity()
{
}

int client_filter_model::desktop() const
{
    return m_desktop.value_or(0);
}

void client_filter_model::setDesktop(int desktop)
{
    if (m_desktop != desktop) {
        m_desktop = desktop;
        Q_EMIT desktopChanged();
        invalidateFilter();
    }
}

void client_filter_model::resetDesktop()
{
    if (m_desktop.has_value()) {
        m_desktop.reset();
        Q_EMIT desktopChanged();
        invalidateFilter();
    }
}

QString client_filter_model::filter() const
{
    return m_filter;
}

void client_filter_model::setFilter(const QString& filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
    invalidateFilter();
}

QString client_filter_model::screenName() const
{
    return m_screenName.value_or(QString());
}

void client_filter_model::setScreenName(const QString& screen)
{
    if (m_screenName != screen) {
        m_screenName = screen;
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

void client_filter_model::resetScreenName()
{
    if (m_screenName.has_value()) {
        m_screenName.reset();
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

client_filter_model::WindowTypes client_filter_model::windowType() const
{
    return m_windowType.value_or(WindowTypes());
}

void client_filter_model::setWindowType(WindowTypes windowType)
{
    if (m_windowType != windowType) {
        m_windowType = windowType;
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

void client_filter_model::resetWindowType()
{
    if (m_windowType.has_value()) {
        m_windowType.reset();
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

bool client_filter_model::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
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

    auto client = qvariant_cast<window*>(data);
    if (!client) {
        return false;
    }

    if (m_desktop.has_value()) {
        if (!client->x11DesktopIds().contains(*m_desktop)) {
            return false;
        }
    }

    if (m_screenName.has_value()) {
        auto output = base::get_output(base::singleton_interface::platform->get_outputs(),
                                       client->screen());
        if (!output || output->name() != m_screenName) {
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

client_filter_model::WindowTypes client_filter_model::windowTypeMask(window* client) const
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

}
