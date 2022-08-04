/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <functional>

class KGlobalAccelInterface;

namespace KWin::input
{

class keyboard;
class pointer;
class switch_device;
class touch;

class KWIN_EXPORT platform_qobject : public QObject
{
    Q_OBJECT
public:
    platform_qobject(std::function<void(KGlobalAccelInterface*)> accel);
    ~platform_qobject() override;

    std::function<void(KGlobalAccelInterface*)> register_global_accel;

Q_SIGNALS:
    void keyboard_added(KWin::input::keyboard*);
    void pointer_added(KWin::input::pointer*);
    void switch_added(KWin::input::switch_device*);
    void touch_added(KWin::input::touch*);

    void keyboard_removed(KWin::input::keyboard*);
    void pointer_removed(KWin::input::pointer*);
    void switch_removed(KWin::input::switch_device*);
    void touch_removed(KWin::input::touch*);
};

}
