/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/keyboard.h"
#include "event.h"
#include "xkb/keyboard.h"

#include <kwin_export.h>

#include <QObject>

namespace KWin::input
{

class KWIN_EXPORT keyboard : public QObject
{
    Q_OBJECT
public:
    input::platform* platform;
    control::keyboard* control{nullptr};
    std::unique_ptr<xkb::keyboard> xkb;

    keyboard(input::platform* platform);
    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    keyboard(keyboard&& other) noexcept = default;
    keyboard& operator=(keyboard&& other) noexcept = default;
    ~keyboard() override;

Q_SIGNALS:
    void key_changed(key_event);
    void modifiers_changed(modifiers_event);
};

}
