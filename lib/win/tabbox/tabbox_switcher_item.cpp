/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tabbox_switcher_item.h"

#include "base/platform.h"
#include "base/singleton_interface.h"
#include "tabbox_handler.h"
#include "win/screen.h"
#include "win/singleton_interface.h"

#include <QAbstractItemModel>

namespace KWin
{
namespace win
{

tabbox_switcher_item::tabbox_switcher_item(QObject* parent)
    : QObject(parent)
    , m_model(nullptr)
    , m_item(nullptr)
    , m_visible(false)
    , m_all_desktops(false)
    , m_current_index(0)
{
    m_selected_index_connection
        = connect(tabbox_handle, &tabbox_handler::selected_index_changed, [this] {
              if (is_visible()) {
                  set_current_index(tabbox_handle->current_index().row());
              }
          });
    connect(base::singleton_interface::platform,
            &base::platform_qobject::topology_changed,
            this,
            &tabbox_switcher_item::screen_geometry_changed);
}

tabbox_switcher_item::~tabbox_switcher_item()
{
    disconnect(m_selected_index_connection);
}

void tabbox_switcher_item::set_item(QObject* item)
{
    if (m_item == item) {
        return;
    }
    m_item = item;
    Q_EMIT item_changed();
}

void tabbox_switcher_item::set_model(QAbstractItemModel* model)
{
    m_model = model;
    Q_EMIT model_changed();
}

void tabbox_switcher_item::set_visible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    if (visible)
        Q_EMIT screen_geometry_changed();
    m_visible = visible;
    Q_EMIT visible_changed();
}

QRect tabbox_switcher_item::screen_geometry() const
{
    return singleton_interface::get_current_output_geometry();
}

void tabbox_switcher_item::set_current_index(int index)
{
    if (m_current_index == index) {
        return;
    }
    m_current_index = index;
    if (m_model) {
        tabbox_handle->set_current_index(m_model->index(index, 0));
    }
    Q_EMIT current_index_changed(m_current_index);
}

void tabbox_switcher_item::set_all_desktops(bool all)
{
    if (m_all_desktops == all) {
        return;
    }
    m_all_desktops = all;
    Q_EMIT all_desktops_changed();
}

void tabbox_switcher_item::set_no_modifier_grab(bool set)
{
    if (m_no_modifier_grab == set) {
        return;
    }
    m_no_modifier_grab = set;
    Q_EMIT no_modifier_grab_changed();
}

bool tabbox_switcher_item::get_automatically_hide() const
{
    return is_automatically_hide;
}

void tabbox_switcher_item::set_automatically_hide(bool value)
{
    if (is_automatically_hide == value) {
        return;
    }
    is_automatically_hide = value;
    Q_EMIT automatically_hide_changed();
}

}
}
