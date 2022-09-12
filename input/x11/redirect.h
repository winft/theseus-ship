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

#include <QX11Info>

namespace KWin::input::x11
{

template<typename Platform, typename Space>
class redirect
{
public:
    using type = redirect<Platform, Space>;
    using platform_t = Platform;
    using space_t = Space;
    using window_t = typename space_t::window_t;

    redirect(Platform& platform, Space& space)
        : qobject{std::make_unique<redirect_qobject>()}
        , keyboard{std::make_unique<keyboard_redirect<type>>(this)}
        , pointer{std::make_unique<pointer_redirect<type>>(this)}
        , cursor{std::make_unique<x11::cursor>()}
        , platform{platform}
        , space{space}
        , xinput{std::make_unique<xinput_integration<type>>(QX11Info::display(), *this)}
    {
        platform.xkb.setConfig(kwinApp()->kxkbConfig());
        platform.xkb.reconfigure();
    }

    ~redirect()
    {
        auto const spies = m_spies;
        for (auto spy : spies) {
            delete spy;
        }
    }

    void
    start_interactive_window_selection(std::function<void(typename space_t::window_t*)> callback,
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

    std::unique_ptr<keyboard_redirect<type>> keyboard;
    std::unique_ptr<pointer_redirect<type>> pointer;
    std::unique_ptr<x11::cursor> cursor;

    std::vector<event_spy<type>*> m_spies;

    Platform& platform;
    Space& space;

private:
    std::unique_ptr<xinput_integration<type>> xinput;
    std::unique_ptr<window_selector<type>> window_sel;
};

}
