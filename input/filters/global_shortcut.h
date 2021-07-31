/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

class QTimer;

namespace KWin::input
{

class global_shortcut_filter : public event_filter
{
public:
    global_shortcut_filter();
    ~global_shortcut_filter();

    bool keyEvent(QKeyEvent* event) override;
    bool pointerEvent(QMouseEvent* event, quint32 nativeButton) override;
    bool swipeGestureBegin(int fingerCount, quint32 time) override;
    bool swipeGestureUpdate(QSizeF const& delta, quint32 time) override;
    bool swipeGestureCancelled(quint32 time) override;
    bool swipeGestureEnd(quint32 time) override;
    bool wheelEvent(QWheelEvent* event) override;

private:
    QTimer* m_powerDown = nullptr;
};

}
