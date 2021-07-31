/*
    SPDX-FileCopyrightText: 2017  Martin Graesslin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input.h"

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

class popup_filter : public QObject, public InputEventFilter
{
    Q_OBJECT
public:
    explicit popup_filter();

    bool keyEvent(QKeyEvent* event) override;
    bool pointerEvent(QMouseEvent* event, quint32 nativeButton) override;

private:
    void handle_window_added(win::wayland::window* window);
    void handle_window_removed(Toplevel* window);
    void cancelPopups();

    std::vector<win::wayland::window*> m_popups;
};

}
}
