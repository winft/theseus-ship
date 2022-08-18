/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wayland_console.h"

#include "debug/console/model_helpers.h"
#include "debug/console/window.h"
#include "input/dbus/device_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "input/xkb/keyboard.h"
#include "input_filter.h"
#include "main.h"

#include "ui_debug_console.h"

#include <Wrapland/Server/surface.h>

namespace KWin::debug
{

wayland_console_model::wayland_console_model(QObject* parent)
    : console_model(parent)
{
}

wayland_console_model::~wayland_console_model() = default;

int wayland_console_model::topLevelRowCount() const
{
    return 4;
}

bool wayland_console_model::get_client_count(int parent_id, int& count) const
{
    if (parent_id == s_waylandClientId) {
        count = m_shellClients.size();
        return true;
    }
    return console_model::get_client_count(parent_id, count);
}

bool wayland_console_model::get_property_count(QModelIndex const& parent, int& count) const
{
    auto id = parent.internalId();

    if (id < s_idDistance * (s_x11UnmanagedId + 1)
        || id >= s_idDistance * (s_waylandClientId + 1)) {
        return console_model::get_property_count(parent, count);
    }
    count = window_property_count(this, parent, &wayland_console_model::shellClient);
    return true;
}

bool wayland_console_model::get_client_index(int row,
                                             int column,
                                             int parent_id,
                                             QModelIndex& index) const
{
    // index for a client (second level)
    if (parent_id == s_waylandClientId) {
        index = index_for_window(this, row, column, m_shellClients, s_waylandClientId);
        return true;
    }

    return console_model::get_client_index(row, column, parent_id, index);
}

bool wayland_console_model::get_property_index(int row,
                                               int column,
                                               QModelIndex const& parent,
                                               QModelIndex& index) const
{
    auto id = parent.internalId();

    if (id < s_idDistance * (s_x11UnmanagedId + 1)
        || id >= s_idDistance * (s_waylandClientId + 1)) {
        return console_model::get_property_index(row, column, parent, index);
    }

    index = index_for_property(this, row, column, parent, &wayland_console_model::shellClient);
    return true;
}

QVariant wayland_console_model::get_client_property_data(QModelIndex const& index, int role) const
{
    if (auto window = shellClient(index)) {
        return propertyData(window, index, role);
    }

    return console_model::get_client_property_data(index, role);
}

QVariant wayland_console_model::get_client_data(QModelIndex const& index, int role) const
{
    if (index.parent().internalId() == s_waylandClientId) {
        return window_data(index, role, m_shellClients);
    }

    return console_model::get_client_data(index, role);
}

win::property_window* wayland_console_model::shellClient(QModelIndex const& index) const
{
    return window_for_index(index, m_shellClients, s_waylandClientId);
}

wayland_console_delegate::wayland_console_delegate(QObject* parent)
    : console_delegate(parent)
{
}

QString wayland_console_delegate::displayText(const QVariant& value, const QLocale& locale) const
{
    if (value.userType() == qMetaTypeId<Wrapland::Server::Surface*>()) {
        if (auto s = value.value<Wrapland::Server::Surface*>()) {
            return QStringLiteral("Wrapland::Server::Surface(0x%1)").arg(qulonglong(s), 0, 16);
        } else {
            return QStringLiteral("nullptr");
        }
    }

    return console_delegate::displayText(value, locale);
}

}
