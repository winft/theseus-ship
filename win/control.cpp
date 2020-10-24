/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"
#include "stacking.h"

#include <config-kwin.h>

#include "abstract_client.h"
#include "appmenu.h"
#include "effects.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include <Wrapland/Server/plasma_window.h>

#include <QObject>
#include <QTimer>

#include <cassert>

namespace KWin::win
{

control::control(Toplevel* win)
    : m_win{win}
{
}

control::~control()
{
    assert(!geometry_updates_blocked());
}

void control::setup_tabbox()
{
    assert(!m_tabbox);
#ifdef KWIN_BUILD_TABBOX
    auto abstract_client = dynamic_cast<AbstractClient*>(m_win);
    assert(abstract_client);
    m_tabbox = std::make_shared<TabBox::TabBoxClientImpl>(abstract_client);
#endif
}

bool control::skip_pager() const
{
    return m_skip_pager;
}

void control::set_skip_pager(bool set)
{
    m_skip_pager = set;
}

bool control::skip_switcher() const
{
    return m_skip_switcher;
}

void control::set_skip_switcher(bool set)
{
    m_skip_switcher = set;
}

bool control::skip_taskbar() const
{
    return m_skip_taskbar;
}

void control::set_skip_taskbar(bool set)
{
    m_skip_taskbar = set;
}

bool control::original_skip_taskbar() const
{
    return m_original_skip_taskbar;
}

void control::set_original_skip_taskbar(bool set)
{
    m_original_skip_taskbar = set;
}

std::weak_ptr<TabBox::TabBoxClientImpl> control::tabbox() const
{
    return m_tabbox;
}

bool control::first_in_tabbox() const
{
    return m_first_in_tabbox;
}

void control::set_first_in_tabbox(bool is_first)
{
    m_first_in_tabbox = is_first;
}

QIcon const& control::icon() const
{
    return m_icon;
}

void control::set_icon(QIcon const& icon)
{
    m_icon = icon;
    Q_EMIT m_win->iconChanged();
}

bool control::has_application_menu() const
{
    return ApplicationMenu::self()->applicationMenuEnabled()
        && !m_application_menu.service_name.isEmpty() && !m_application_menu.object_path.isEmpty();
}

bool control::application_menu_active() const
{
    return m_application_menu.active;
}

void control::set_application_menu_active(bool active)
{
    if (m_application_menu.active == active) {
        return;
    }
    m_application_menu.active = active;
    Q_EMIT m_win->applicationMenuActiveChanged(active);
}

QString control::application_menu_service_name() const
{
    return m_application_menu.service_name;
}

QString control::application_menu_object_path() const
{
    return m_application_menu.object_path;
}

void control::update_application_menu_service_name(const QString& name)
{
    auto const had_menu = has_application_menu();
    m_application_menu.service_name = name;
    auto const has_menu_now = has_application_menu();

    if (had_menu != has_menu_now) {
        Q_EMIT m_win->hasApplicationMenuChanged(has_menu_now);
    }
}

void control::update_application_menu_object_path(const QString& path)
{
    auto const had_menu = has_application_menu();
    m_application_menu.object_path = path;
    auto const has_menu_now = has_application_menu();

    if (had_menu != has_menu_now) {
        Q_EMIT m_win->hasApplicationMenuChanged(has_menu_now);
    }
}

bool control::active() const
{
    return m_active;
}

void control::set_active(bool active)
{
    m_active = active;
}

bool control::keep_above() const
{
    return m_keep_above;
}

void control::set_keep_above(bool keep)
{
    m_keep_above = keep;
}

bool control::keep_below() const
{
    return m_keep_below;
}
void control::set_keep_below(bool keep)
{
    m_keep_below = keep;
}

void control::set_demands_attention(bool set)
{
    m_demands_attention = set;
}

bool control::demands_attention() const
{
    return m_demands_attention;
}

bool control::unresponsive() const
{
    return m_unresponsive;
}

void control::set_unresponsive(bool unresponsive)
{
    if (m_unresponsive == unresponsive) {
        return;
    }
    m_unresponsive = unresponsive;
    Q_EMIT m_win->unresponsiveChanged(m_unresponsive);
    Q_EMIT m_win->captionChanged();
}

void control::start_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = new QTimer(m_win);
    QObject::connect(m_auto_raise_timer, &QTimer::timeout, m_win, [this] { auto_raise(m_win); });
    m_auto_raise_timer->setSingleShot(true);
    m_auto_raise_timer->start(options->autoRaiseInterval());
}

void control::cancel_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = nullptr;
}

bool control::minimized() const
{
    return m_minimized;
}

void control::set_minimized(bool minimize)
{
    m_minimized = minimize;
}

void control::update_mouse_grab()
{
}

Wrapland::Server::PlasmaWindow* control::wayland_management() const
{
    return m_wayland_management;
}

void control::set_wayland_management(Wrapland::Server::PlasmaWindow* plasma_window)
{
    m_wayland_management = plasma_window;
}

void control::destroy_wayland_management()
{
    if (m_wayland_management) {
        m_wayland_management->unmap();
        m_wayland_management = nullptr;
    }
}

bool control::have_resize_effect() const
{
    return m_have_resize_effect;
}

void control::update_have_resize_effect()
{
    m_have_resize_effect
        = effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::Resize);
}

void control::reset_have_resize_effect()
{
    m_have_resize_effect = false;
}

bool control::geometry_updates_blocked() const
{
    return m_block_geometry_updates != 0;
}

void control::block_geometry_updates()
{
    m_block_geometry_updates++;
}

void control::unblock_geometry_updates()
{
    m_block_geometry_updates--;
}

pending_geometry control::pending_geometry_update() const
{
    return m_pending_geometry_update;
}

void control::set_pending_geometry_update(pending_geometry update)
{
    m_pending_geometry_update = update;
}

QRect control::buffer_geometry_before_update_blocking() const
{
    return m_buffer_geometry_before_update_blocking;
}

QRect control::frame_geometry_before_update_blocking() const
{
    return m_frame_geometry_before_update_blocking;
}

void control::update_geometry_before_update_blocking()
{
    m_buffer_geometry_before_update_blocking = m_win->bufferGeometry();
    m_frame_geometry_before_update_blocking = m_win->frameGeometry();
}

QRect control::visible_rect_before_geometry_update() const
{
    return m_visible_rect_before_geometry_update;
}

void control::set_visible_rect_before_geometry_update(QRect const& rect)
{
    m_visible_rect_before_geometry_update = rect;
}

}
