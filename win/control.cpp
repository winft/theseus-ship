/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"

#include "dbus/appmenu.h"
#include "deco/client_impl.h"
#include "deco/palette.h"
#include "deco/window.h"
#include "stacking.h"

#include <config-kwin.h>

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

#include "render/effects.h"
#include "toplevel.h"

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
    assert(m_deco.decoration == nullptr);
}

void control::setup_tabbox()
{
    assert(!m_tabbox);
#if KWIN_BUILD_TABBOX
    m_tabbox = std::make_shared<win::tabbox_client_impl>(m_win);
#endif
}

void control::setup_color_scheme()
{
    m_palette.color_scheme = QStringLiteral("kdeglobals");
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

std::weak_ptr<win::tabbox_client_impl> control::tabbox() const
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

QByteArray control::desktop_file_name() const
{
    return m_desktop_file_name;
}

void control::set_desktop_file_name(QByteArray const& name)
{
    m_desktop_file_name = name;
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
    return m_win->space.appmenu->applicationMenuEnabled() && !appmenu.address.empty();
}

bool control::application_menu_active() const
{
    return appmenu.active;
}

void control::set_application_menu_active(bool active)
{
    if (appmenu.active == active) {
        return;
    }
    appmenu.active = active;
    Q_EMIT m_win->applicationMenuActiveChanged(active);
}

appmenu control::application_menu() const
{
    return appmenu;
}

void control::update_application_menu(appmenu_address const& address)
{
    if (address == appmenu.address) {
        return;
    }

    auto const had_menu = has_application_menu();

    appmenu.address = address;
    Q_EMIT m_win->applicationMenuChanged();

    auto const has_menu = has_application_menu();

    if (had_menu != has_menu) {
        Q_EMIT m_win->hasApplicationMenuChanged(has_menu);
    }
}

QKeySequence const& control::shortcut() const
{
    return m_shortcut;
}
void control::set_shortcut(QString const& shortcut)
{
    m_shortcut = QKeySequence::fromString(shortcut);
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
    m_auto_raise_timer->start(kwinApp()->options->autoRaiseInterval());
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
    auto& effects = m_win->space.render.effects;
    m_have_resize_effect = effects && effects->provides(Effect::Resize);
}

void control::reset_have_resize_effect()
{
    m_have_resize_effect = false;
}

QSize control::adjusted_frame_size(QSize const& frame_size, [[maybe_unused]] size_mode mode)
{
    auto const border_size = win::frame_size(m_win);

    auto const min_size = m_win->minSize() + border_size;
    auto max_size = m_win->maxSize();

    // Maximum size need to be checked for overflow.
    if (INT_MAX - border_size.width() >= max_size.width()) {
        max_size.setWidth(max_size.width() + border_size.width());
    }
    if (INT_MAX - border_size.height() >= max_size.height()) {
        max_size.setWidth(max_size.height() + border_size.height());
    }

    return frame_size.expandedTo(min_size).boundedTo(max_size);
}

quicktiles control::electric() const
{
    return m_electric;
}
void control::set_electric(quicktiles tiles)
{
    m_electric = tiles;
}

bool control::electric_maximizing() const
{
    return m_electric_maximizing;
}
void control::set_electric_maximizing(bool maximizing)
{
    m_electric_maximizing = maximizing;
}

QTimer* control::electric_maximizing_timer() const
{
    return m_electric_maximizing_delay;
}

void control::set_electric_maximizing_timer(QTimer* timer)
{
    assert(!m_electric_maximizing_delay);
    m_electric_maximizing_delay = timer;
}

quicktiles control::quicktiling() const
{
    return m_quicktiling;
}
void control::set_quicktiling(quicktiles tiles)
{
    m_quicktiling = tiles;
}

bool control::can_fullscreen() const
{
    return false;
}

bool control::fullscreen() const
{
    return m_fullscreen;
}

void control::set_fullscreen(bool fullscreen)
{
    m_fullscreen = fullscreen;
}

win::move_resize_op& control::move_resize()
{
    return m_move_resize;
}

win::deco_impl& control::deco()
{
    return m_deco;
}

void control::destroy_decoration()
{
    QObject::disconnect(m_deco.client_destroy);
    delete m_deco.decoration;
    m_deco.decoration = nullptr;

    delete m_deco.window;
    m_deco.window = nullptr;
}

win::palette& control::palette()
{
    return m_palette;
}

WindowRules& control::rules()
{
    return m_rules;
}

WindowRules const& control::rules() const
{
    return m_rules;
}

void control::set_rules(WindowRules const& rules)
{
    m_rules = rules;
}

void control::remove_rule(Rules* rule)
{
    m_rules.remove(rule);
}

void control::discard_temporary_rules()
{
    m_rules.discardTemporary();
}

}
