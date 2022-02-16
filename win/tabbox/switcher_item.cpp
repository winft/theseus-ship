/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "switcher_item.h"

#include "base/platform.h"
#include "main.h"
#include "render/compositor.h"
#include "screens.h"
#include "tabbox_handler.h"
// Qt
#include <QAbstractItemModel>

namespace KWin
{
namespace win
{

SwitcherItem::SwitcherItem(QObject* parent)
    : QObject(parent)
    , m_model(nullptr)
    , m_item(nullptr)
    , m_visible(false)
    , m_all_desktops(false)
    , m_current_index(0)
{
    m_selected_index_connection = connect(tabBox, &TabBoxHandler::selected_index_changed, [this] {
        if (is_visible()) {
            set_current_index(tabBox->current_index().row());
        }
    });
    connect(&kwinApp()->get_base().screens,
            &Screens::changed,
            this,
            &SwitcherItem::screen_geometry_changed);
    connect(render::compositor::self(),
            &render::compositor::compositingToggled,
            this,
            &SwitcherItem::compositing_changed);
}

SwitcherItem::~SwitcherItem()
{
    disconnect(m_selected_index_connection);
}

void SwitcherItem::set_item(QObject* item)
{
    if (m_item == item) {
        return;
    }
    m_item = item;
    Q_EMIT item_changed();
}

void SwitcherItem::set_model(QAbstractItemModel* model)
{
    m_model = model;
    Q_EMIT model_changed();
}

void SwitcherItem::set_visible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    if (visible)
        Q_EMIT screen_geometry_changed();
    m_visible = visible;
    Q_EMIT visible_changed();
}

QRect SwitcherItem::screen_geometry() const
{
    auto& screens = kwinApp()->get_base().screens;
    return screens.geometry(screens.current());
}

void SwitcherItem::set_current_index(int index)
{
    if (m_current_index == index) {
        return;
    }
    m_current_index = index;
    if (m_model) {
        tabBox->set_current_index(m_model->index(index, 0));
    }
    Q_EMIT current_index_changed(m_current_index);
}

void SwitcherItem::set_all_desktops(bool all)
{
    if (m_all_desktops == all) {
        return;
    }
    m_all_desktops = all;
    Q_EMIT all_desktops_changed();
}

void SwitcherItem::set_no_modifier_grab(bool set)
{
    if (m_no_modifier_grab == set) {
        return;
    }
    m_no_modifier_grab = set;
    Q_EMIT no_modifier_grab_changed();
}

bool SwitcherItem::compositing()
{
    return render::compositor::compositing();
}

}
}
