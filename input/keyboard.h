/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/keyboard.h"
#include "event.h"
#include "xkb/keyboard.h"

#include "kwin_export.h"

#include <QObject>

namespace KWin::input
{

class KWIN_EXPORT keyboard : public QObject
{
    Q_OBJECT
public:
    input::platform* platform;
    std::unique_ptr<control::keyboard> control;
    std::unique_ptr<xkb::keyboard> xkb;

    keyboard(input::platform* platform);
    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    ~keyboard() override;

Q_SIGNALS:
    void key_changed(key_event);
    void modifiers_changed(modifiers_event);
};

}
