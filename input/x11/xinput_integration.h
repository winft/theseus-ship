/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

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
class platform;

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

    struct {
        std::unique_ptr<input::pointer> pointer;
        std::unique_ptr<input::keyboard> keyboard;
    } fake_devices;

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
