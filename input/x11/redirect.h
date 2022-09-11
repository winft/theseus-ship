/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"
#include "cursor.h"
#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "window_selector.h"

#include "input/redirect_qobject.h"

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
        , platform{platform}
        , space{space}
    {
        platform.redirect = this;
        create_cursor();
        pointer = std::make_unique<pointer_redirect<type>>(this);
        keyboard = std::make_unique<keyboard_redirect<type>>(this);
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

    std::unique_ptr<x11::cursor> cursor;
    std::unique_ptr<keyboard_redirect<type>> keyboard;
    std::unique_ptr<pointer_redirect<type>> pointer;

    std::vector<event_spy<type>*> m_spies;

    std::unique_ptr<redirect_qobject> qobject;
    Platform& platform;
    Space& space;

private:
#if HAVE_X11_XINPUT
    void create_cursor()
    {
        auto const is_xinput_avail = platform.xinput != nullptr;
        this->cursor = std::make_unique<x11::cursor>(is_xinput_avail);

        if (is_xinput_avail) {
            platform.xinput->setCursor(static_cast<x11::cursor*>(this->cursor.get()));

            platform.xkb.setConfig(kwinApp()->kxkbConfig());
            platform.xkb.reconfigure();
        }
    }
#else
    void create_cursor()
    {
        cursor = std::make_unique<x11::cursor>(false);
    }
#endif
    std::unique_ptr<window_selector<type>> window_sel;
};

}
