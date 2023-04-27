/*
    SPDX-FileCopyrightText: 2017 Martin Graesslin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/window_find.h"
#include "win/deco.h"
#include "win/geo.h"
#include "win/transient.h"
#include "win/util.h"

#include <Wrapland/Server/keyboard.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <QObject>

namespace KWin::input
{

template<typename Redirect>
class popup_filter : public QObject, public event_filter<Redirect>
{
public:
    using space_t = typename Redirect::space_t;

    explicit popup_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
        QObject::connect(redirect.space.qobject.get(),
                         &win::space_qobject::wayland_window_added,
                         this,
                         [this](auto win_id) {
                             auto win = this->redirect.space.windows_map.at(win_id);
                             handle_window_added(std::get<typename space_t::wayland_window*>(win));
                         });
    }

    bool button(button_event const& event) override
    {
        if (m_popups.empty()) {
            return false;
        }

        switch (event.state) {
        case button_state::released:
            return false;
        case button_state::pressed:
            auto pos = this->redirect.globalPointer();
            auto focus_window = find_window(this->redirect, pos.toPoint());
            if (!focus_window) {
                cancelPopups();
                return true;
            }

            if (std::visit(overload{[&](auto&& win) {
                               if (!win::belong_to_same_client(win, m_popups.back())) {
                                   //  Press on an app window not belonging to the popup, filter
                                   //  out this press.
                                   cancelPopups();
                                   return true;
                               }
                               if (win::decoration(win)) {
                                   // Test whether it is on the decoration.
                                   auto const content_rect
                                       = win->geo.frame - win::frame_margins(win);
                                   if (!content_rect.contains(pos.toPoint())) {
                                       cancelPopups();
                                       return true;
                                   }
                               }
                               return false;
                           }},
                           *focus_window)) {
                return true;
            }
        }

        return false;
    }

    bool key(key_event const& event) override
    {
        if (m_popups.empty()) {
            return false;
        }

        auto seat = this->redirect.platform.base.server->seat();

        auto last = m_popups.back();
        if (!last->surface) {
            return false;
        }

        seat->setFocusedKeyboardSurface(last->surface);
        pass_to_wayland_server(this->redirect, event);
        return true;
    }

    bool key_repeat(key_event const& /*event*/) override
    {
        // Filter out event when a popup is active.
        // TODO(romangg): Are we supposed to do something more with a key repeat? But the clients
        // are
        //                handling key repeats themselves.
        return !m_popups.empty() && m_popups.back()->surface;
    }

private:
    void handle_window_added(typename space_t::wayland_window* window)
    {
        if (contains(m_popups, window)) {
            return;
        }
        if (window->transient->input_grab) {
            // TODO: verify that the Toplevel is allowed as a popup
            connect(
                window->qobject.get(),
                &win::window_qobject::windowShown,
                this,
                [this, window] { handle_window_added(window); },
                Qt::UniqueConnection);
            connect(
                window->qobject.get(),
                &win::window_qobject::closed,
                this,
                [this, window] { remove_all(m_popups, window); },
                Qt::UniqueConnection);
            m_popups.push_back(window);
        }
    }

    void cancelPopups()
    {
        while (!m_popups.empty()) {
            m_popups.back()->cancel_popup();
            m_popups.pop_back();
        }
    }

    std::vector<typename space_t::wayland_window*> m_popups;
};

}
