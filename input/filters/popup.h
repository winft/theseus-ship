/*
    SPDX-FileCopyrightText: 2017  Martin Graesslin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/redirect.h"
#include "main.h"
#include "win/deco.h"
#include "win/geo.h"
#include "win/transient.h"
#include "win/util.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

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
    using wayland_space = win::wayland::space<base::wayland::platform>;
    using wayland_window = win::wayland::window<wayland_space>;

    explicit popup_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
        QObject::connect(
            redirect.space.qobject.get(),
            &win::space::qobject_t::wayland_window_added,
            this,
            [this](auto window) { handle_window_added(static_cast<wayland_window*>(window)); });
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
            auto focus_window = this->redirect.findToplevel(pos.toPoint());
            if (!focus_window || !win::belong_to_same_client(focus_window, m_popups.back())) {
                // a press on a window (or no window) not belonging to the popup window
                cancelPopups();
                // filter out this press
                return true;
            }
            if (focus_window && win::decoration(focus_window)) {
                // Test whether it is on the decoration.
                auto const content_rect
                    = focus_window->frameGeometry() - win::frame_margins(focus_window);
                if (!content_rect.contains(pos.toPoint())) {
                    cancelPopups();
                    return true;
                }
            }
        }

        return false;
    }

    bool key(key_event const& event) override
    {
        if (m_popups.empty()) {
            return false;
        }

        auto seat = waylandServer()->seat();

        auto last = m_popups.back();
        if (!last->surface) {
            return false;
        }

        seat->setFocusedKeyboardSurface(last->surface);
        pass_to_wayland_server(event);
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
    void handle_window_added(wayland_window* window)
    {
        if (contains(m_popups, window)) {
            return;
        }
        if (window->transient()->input_grab) {
            // TODO: verify that the Toplevel is allowed as a popup
            connect(
                window,
                &Toplevel::windowShown,
                this,
                [this, window] { handle_window_added(window); },
                Qt::UniqueConnection);
            connect(
                window,
                &Toplevel::closed,
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

    std::vector<wayland_window*> m_popups;
};

}
