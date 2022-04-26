/*
    SPDX-FileCopyrightText: 2017  Martin Graesslin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

#include <QObject>

namespace KWin
{
class Toplevel;

namespace win::wayland
{
class window;
}

namespace input
{

class popup_filter : public QObject, public event_filter
{
    Q_OBJECT
public:
    explicit popup_filter();

    bool key(key_event const& event) override;
    bool key_repeat(key_event const& event) override;

    bool button(button_event const& event) override;

private:
    void handle_window_added(win::wayland::window* window);
    void cancelPopups();

    std::vector<win::wayland::window*> m_popups;
};

}
}
