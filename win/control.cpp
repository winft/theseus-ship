/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"

#include "dbus/appmenu.h"
#include "deco/client_impl.h"
#include "deco/palette.h"
#include "deco/window.h"
#include "space.h"
#include "stacking.h"

#include <config-kwin.h>

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

#include "render/compositor.h"
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
    assert(deco.decoration == nullptr);
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
    palette.color_scheme = QStringLiteral("kdeglobals");
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

std::weak_ptr<win::tabbox_client_impl> control::tabbox() const
{
    return m_tabbox;
}

void control::set_icon(QIcon const& icon)
{
    this->icon = icon;
    Q_EMIT m_win->qobject->iconChanged();
}

bool control::has_application_menu() const
{
    return m_win->space.appmenu->applicationMenuEnabled() && !appmenu.address.empty();
}

void control::set_application_menu_active(bool active)
{
    if (appmenu.active == active) {
        return;
    }
    appmenu.active = active;
    Q_EMIT m_win->qobject->applicationMenuActiveChanged(active);
}

void control::update_application_menu(appmenu_address const& address)
{
    if (address == appmenu.address) {
        return;
    }

    auto const had_menu = has_application_menu();

    appmenu.address = address;
    Q_EMIT m_win->qobject->applicationMenuChanged();

    auto const has_menu = has_application_menu();

    if (had_menu != has_menu) {
        Q_EMIT m_win->qobject->hasApplicationMenuChanged(has_menu);
    }
}

void control::set_shortcut(QString const& shortcut)
{
    this->shortcut = QKeySequence::fromString(shortcut);
}

void control::set_unresponsive(bool unresponsive)
{
    if (this->unresponsive == unresponsive) {
        return;
    }
    this->unresponsive = unresponsive;
    Q_EMIT m_win->qobject->unresponsiveChanged(unresponsive);
    Q_EMIT m_win->qobject->captionChanged();
}

void control::start_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = new QTimer(m_win->qobject.get());
    QObject::connect(
        m_auto_raise_timer, &QTimer::timeout, m_win->qobject.get(), [this] { auto_raise(m_win); });
    m_auto_raise_timer->setSingleShot(true);
    m_auto_raise_timer->start(kwinApp()->options->qobject->autoRaiseInterval());
}

void control::cancel_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = nullptr;
}

void control::update_mouse_grab()
{
}

void control::destroy_plasma_wayland_integration()
{
    if (plasma_wayland_integration) {
        plasma_wayland_integration->unmap();
        plasma_wayland_integration = nullptr;
    }
}

void control::update_have_resize_effect()
{
    auto& effects = m_win->space.render.effects;
    have_resize_effect = effects && effects->provides(Effect::Resize);
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

bool control::can_fullscreen() const
{
    return false;
}

void control::destroy_decoration()
{
    QObject::disconnect(deco.client_destroy);
    delete deco.decoration;
    deco.decoration = nullptr;

    delete deco.window;
    deco.window = nullptr;
}

void control::remove_rule(rules::ruling* rule)
{
    rules.remove(rule);
}

void control::discard_temporary_rules()
{
    rules.discardTemporary();
}

}
