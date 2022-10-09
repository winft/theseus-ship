/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_client.h"

#include "utils/algorithm.h"
#include "win/meta.h"

#include <variant>

namespace KWin::win
{

template<typename Window>
class tabbox_client_impl : public tabbox_client
{
public:
    explicit tabbox_client_impl(Window window)
        : window{window}
    {
    }

    QString caption() const override
    {
        return std::visit(overload{[](auto&& win) {
                              if (win::is_desktop(win)) {
                                  return i18nc(
                                      "Special entry in alt+tab list for minimizing all windows",
                                      "Show Desktop");
                              }
                              return win::caption(win);
                          }},
                          window);
    }

    QIcon icon() const override
    {
        return std::visit(overload{[](auto&& win) {
                              if (win::is_desktop(win)) {
                                  return QIcon::fromTheme(QStringLiteral("user-desktop"));
                              }
                              return win->control->icon;
                          }},
                          window);
    }

    bool is_minimized() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->minimized; }}, window);
    }

    int x() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.pos().x(); }}, window);
    }

    int y() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.pos().y(); }}, window);
    }

    int width() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.size().width(); }}, window);
    }

    int height() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.size().height(); }}, window);
    }

    bool is_closeable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isCloseable(); }}, window);
    }

    void close() override
    {
        std::visit(overload{[](auto&& win) { win->closeWindow(); }}, window);
    }

    bool is_first_in_tabbox() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->first_in_tabbox; }},
                          window);
    }

    QUuid internal_id() const override
    {
        return std::visit(overload{[](auto&& win) { return win->meta.internal_id; }}, window);
    }

    Window client() const
    {
        return window;
    }

private:
    Window window;
};

}
