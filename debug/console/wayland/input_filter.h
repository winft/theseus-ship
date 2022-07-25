/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_spy.h"

class QTextEdit;

namespace KWin::debug
{

class input_filter : public input::event_spy
{
public:
    input_filter(input::redirect& redirect, QTextEdit* textEdit);

    void button(input::button_event const& event) override;
    void motion(input::motion_event const& event) override;
    void axis(input::axis_event const& event) override;

    void key(input::key_event const& event) override;
    void key_repeat(input::key_event const& event) override;

    void touch_down(input::touch_down_event const& event) override;
    void touch_motion(input::touch_motion_event const& event) override;
    void touch_up(input::touch_up_event const& event) override;

    void pinch_begin(input::pinch_begin_event const& event) override;
    void pinch_update(input::pinch_update_event const& event) override;
    void pinch_end(input::pinch_end_event const& event) override;

    void swipe_begin(input::swipe_begin_event const& event) override;
    void swipe_update(input::swipe_update_event const& event) override;
    void swipe_end(input::swipe_end_event const& event) override;

    void switch_toggle(input::switch_toggle_event const& event) override;

    void tabletToolEvent(QTabletEvent* event) override;
    void tabletToolButtonEvent(const QSet<uint>& pressedButtons) override;
    void tabletPadButtonEvent(const QSet<uint>& pressedButtons) override;
    void tabletPadStripEvent(int number, int position, bool isFinger) override;
    void tabletPadRingEvent(int number, int position, bool isFinger) override;

private:
    QTextEdit* m_textEdit;
};

}
