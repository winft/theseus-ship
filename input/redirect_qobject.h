/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"

#include "kwin_export.h"

#include <QObject>
#include <QPointF>

namespace KWin::input
{

class KWIN_EXPORT redirect_qobject : public QObject
{
    Q_OBJECT
public:
    ~redirect_qobject() override;

Q_SIGNALS:
    void globalPointerChanged(QPointF const& pos);
    void pointerButtonStateChanged(uint32_t button, button_state state);

    /**
     * Only emitted for the mask which is provided by Qt::KeyboardModifiers, if other modifiers
     * change signal is not emitted
     */
    void keyboardModifiersChanged(Qt::KeyboardModifiers newMods, Qt::KeyboardModifiers oldMods);
    void keyStateChanged(quint32 keyCode, key_state state);

    void has_tablet_mode_switch_changed(bool set);
};

}
