/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <input/cursor.h>

#include <kwinglobals.h>

#include <QObject>
#include <QPointF>

namespace KWin::input::wayland
{

/**
 * @brief Implementation using the InputRedirection framework to get pointer positions.
 *
 * Does not support warping of cursor.
 */
class KWIN_EXPORT cursor : public input::cursor
{
    Q_OBJECT
public:
    cursor();

    PlatformCursorImage platform_image() const override;

protected:
    void do_set_pos() override;

    void do_start_image_tracking() override;
    void do_stop_image_tracking() override;

private:
    void slot_pos_changed(const QPointF& pos);
    void slot_pointer_button_changed();
    void slot_modifiers_changed(Qt::KeyboardModifiers mods, Qt::KeyboardModifiers oldMods);

    Qt::MouseButtons m_currentButtons;
    friend class input::cursor;
};

}
