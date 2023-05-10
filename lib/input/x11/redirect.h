/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"
#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "window_selector.h"
#include "xinput_integration.h"

#include "input/redirect_qobject.h"

#include <QtGui/private/qtx11extras_p.h>

namespace KWin::input::x11
{

template<typename Space>
class redirect
{
public:
    using type = redirect<Space>;
    using platform_t = typename Space::base_t::input_t;
    using space_t = Space;
    using window_t = typename space_t::window_t;
    using event_spy_t = input::event_spy<type>;

    redirect(Space& space)
        : qobject{std::make_unique<redirect_qobject>()}
        , platform{*space.base.input}
        , keyboard{std::make_unique<keyboard_redirect<type>>(this)}
        , pointer{std::make_unique<pointer_redirect<type>>(this)}
        , cursor{std::make_unique<x11::cursor>(platform.base.x11_data,
                                               *platform.base.x11_event_filters,
                                               platform.config.main)}
        , space{space}
        , xinput{std::make_unique<xinput_integration<type>>(QX11Info::display(), *this)}
    {
        platform.xkb.reconfigure();
    }

    ~redirect()
    {
        auto const spies = m_spies;
        for (auto spy : spies) {
            delete spy;
        }
    }

    void start_interactive_window_selection(std::function<void(std::optional<window_t>)> callback,
                                            QByteArray const& cursorName = QByteArray())
    {
        if (!window_sel) {
            window_sel = std::make_unique<window_selector<type>>(*this);
        }
        window_sel->start(callback, cursorName);
    }

    void start_interactive_position_selection(std::function<void(QPoint const&)> callback)
    {
        if (!window_sel) {
            window_sel = std::make_unique<window_selector<type>>(*this);
        }
        window_sel->start(callback);
    }

    std::unique_ptr<redirect_qobject> qobject;
    platform_t& platform;

    std::unique_ptr<keyboard_redirect<type>> keyboard;
    std::unique_ptr<pointer_redirect<type>> pointer;
    std::unique_ptr<x11::cursor> cursor;

    std::vector<event_spy_t*> m_spies;

    Space& space;
    std::unique_ptr<xinput_integration<type>> xinput;

private:
    std::unique_ptr<window_selector<type>> window_sel;
};

}
