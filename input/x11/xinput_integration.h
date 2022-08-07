/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "platform.h"

#include "input/keyboard.h"
#include "input/pointer.h"

#include <QObject>
#include <QPointer>
#include <QScopedPointer>
#include <memory>

typedef struct _XDisplay Display;

namespace KWin::input
{
class keyboard;
class pointer;

namespace x11
{

class XInputEventFilter;
class XKeyPressReleaseEventFilter;
class cursor;

struct xinput_devices {
    xinput_devices(x11::platform& platform)
        : keyboard{std::make_unique<input::keyboard>(platform.xkb.context,
                                                     platform.xkb.compose_table)}
        , pointer{std::make_unique<input::pointer>()}
        , platform{platform}
    {
        platform_add_keyboard(keyboard.get(), platform);
        platform_add_pointer(pointer.get(), platform);
    }

    ~xinput_devices()
    {
        platform_remove_pointer(pointer.get(), platform);
        platform_remove_keyboard(keyboard.get(), platform);
    }

    std::unique_ptr<input::keyboard> keyboard;
    std::unique_ptr<input::pointer> pointer;
    x11::platform& platform;
};

class xinput_integration : public QObject
{
    Q_OBJECT
public:
    explicit xinput_integration(Display* display, x11::platform* platform);
    ~xinput_integration() override;

    void init();
    void startListening();

    bool hasXinput() const
    {
        return m_hasXInput;
    }
    void setCursor(cursor* cursor);

    xinput_devices fake_devices;

    x11::platform* platform;

private:
    void setup_fake_devices();
    Display* display() const
    {
        return m_x11Display;
    }

    bool m_hasXInput = false;
    int m_xiOpcode = 0;
    int m_majorVersion = 0;
    int m_minorVersion = 0;
    QPointer<cursor> m_x11Cursor;
    Display* m_x11Display;

    QScopedPointer<XInputEventFilter> m_xiEventFilter;
    QScopedPointer<XKeyPressReleaseEventFilter> m_keyPressFilter;
    QScopedPointer<XKeyPressReleaseEventFilter> m_keyReleaseFilter;
};

}
}
