/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "appmenu.h"
#include "config-kwin.h"
#include "geo.h"
#include "stacking.h"
#include "structs.h"
#include "tabbox/tabbox_client_impl.h"
#include "types.h"
#include "virtual_desktops.h"

#include "rules/window.h"
#include "scripting/window.h"

#include <kwineffects/effect.h>

#include <QIcon>
#include <QKeySequence>
#include <QRect>

#include <memory>

namespace Wrapland::Server
{
class PlasmaWindow;
}

namespace KWin::win
{

template<typename Window>
class control
{
public:
    using var_win = typename Window::space_t::window_t;

    explicit control(Window* win)
        : m_win{win}
    {
    }

    virtual ~control()
    {
        assert(deco.decoration == nullptr);
    }

    void setup_tabbox()
    {
        assert(!m_tabbox);
#if KWIN_BUILD_TABBOX
        m_tabbox = std::make_shared<win::tabbox_client_impl<var_win>>(m_win);
#endif
    }

    virtual void set_desktops(QVector<virtual_desktop*> desktops) = 0;

    bool skip_pager() const
    {
        return m_skip_pager;
    }

    virtual void set_skip_pager(bool set)
    {
        m_skip_pager = set;
    }

    bool skip_switcher() const
    {
        return m_skip_switcher;
    }

    virtual void set_skip_switcher(bool set)
    {
        m_skip_switcher = set;
    }

    bool skip_taskbar() const
    {
        return m_skip_taskbar;
    }

    virtual void set_skip_taskbar(bool set)
    {
        m_skip_taskbar = set;
    }

    std::weak_ptr<win::tabbox_client_impl<var_win>> tabbox() const
    {
        return m_tabbox;
    }

    bool has_application_menu() const
    {
        return m_win->space.appmenu->applicationMenuEnabled() && !appmenu.address.empty();
    }

    void set_application_menu_active(bool active)
    {
        if (appmenu.active == active) {
            return;
        }
        appmenu.active = active;
        Q_EMIT m_win->qobject->applicationMenuActiveChanged(active);
    }

    void update_application_menu(appmenu_address const& address)
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

    void set_shortcut(QString const& shortcut)
    {
        this->shortcut = QKeySequence::fromString(shortcut);
    }

    void set_unresponsive(bool unresponsive)
    {
        if (this->unresponsive == unresponsive) {
            return;
        }
        this->unresponsive = unresponsive;
        Q_EMIT m_win->qobject->unresponsiveChanged(unresponsive);
        Q_EMIT m_win->qobject->captionChanged();
    }

    void start_auto_raise()
    {
        delete m_auto_raise_timer;
        m_auto_raise_timer = new QTimer(m_win->qobject.get());
        QObject::connect(m_auto_raise_timer, &QTimer::timeout, m_win->qobject.get(), [this] {
            auto_raise(*m_win);
        });
        m_auto_raise_timer->setSingleShot(true);
        m_auto_raise_timer->start(m_win->space.base.options->qobject->autoRaiseInterval());
    }

    void cancel_auto_raise()
    {
        delete m_auto_raise_timer;
        m_auto_raise_timer = nullptr;
    }

    virtual void update_mouse_grab()
    {
    }

    virtual void destroy_plasma_wayland_integration()
    {
    }

    void update_have_resize_effect()
    {
        auto& effects = m_win->space.base.render->compositor->effects;
        have_resize_effect = effects && effects->provides(Effect::Resize);
    }

    virtual QSize adjusted_frame_size(QSize const& frame_size, size_mode /*mode*/)
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

    virtual bool can_fullscreen() const
    {
        return false;
    }

    virtual void destroy_decoration()
    {
        QObject::disconnect(deco.client_destroy);
        delete deco.decoration;
        deco.decoration = nullptr;

        delete deco.window;
        deco.window = nullptr;
    }

    void setup_color_scheme()
    {
        palette.color_scheme = QStringLiteral("kdeglobals");
    }

    void remove_rule(rules::ruling* rule)
    {
        rules.remove(rule);
    }

    using scripting_t = scripting::window_impl<var_win>;
    std::unique_ptr<scripting_t> scripting;
    Wrapland::Server::PlasmaWindow* plasma_wayland_integration{nullptr};

    bool active{false};
    bool keep_above{false};
    bool keep_below{false};
    bool demands_attention{false};
    bool unresponsive{false};
    bool original_skip_taskbar{false};

    win::appmenu appmenu;
    QKeySequence shortcut;
    QIcon icon;

    quicktiles quicktiling{quicktiles::none};
    quicktiles electric{quicktiles::none};
    bool electric_maximizing{false};
    QTimer* electric_maximizing_delay{nullptr};

    bool have_resize_effect{false};

    bool first_in_tabbox{false};
    QByteArray desktop_file_name;

    bool fullscreen{false};
    bool minimized{false};
    win::move_resize_op move_resize;
    win::deco_impl<Window, var_win> deco;
    win::palette palette;
    rules::window rules;

private:
    void minimize(bool avoid_animation);
    void unminimize(bool avoid_animation);

    bool m_skip_taskbar{false};
    bool m_skip_pager{false};
    bool m_skip_switcher{false};

    std::shared_ptr<win::tabbox_client_impl<var_win>> m_tabbox;

    QTimer* m_auto_raise_timer{nullptr};

    Window* m_win;
};

}
