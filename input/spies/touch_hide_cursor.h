/*
    SPDX-FileCopyrightText: 2018 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_spy.h"

namespace KWin::input
{

class touch_hide_cursor_spy : public event_spy
{
public:
    void pointerEvent(MouseEvent* event) override;
    void wheelEvent(WheelEvent* event) override;
    void touchDown(qint32 id, const QPointF& pos, quint32 time) override;

private:
    void showCursor();
    void hideCursor();

    bool m_cursorHidden = false;
};

}
