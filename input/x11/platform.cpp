/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "cursor.h"
#include "window_selector.h"

#if HAVE_X11_XINPUT
#include "input/x11/xinput_integration.h"
#endif

#include "input/keyboard_redirect.h"
#include "input/logging.h"
#include "input/x11/redirect.h"
#include "main.h"

#include <QX11Info>

namespace KWin::input::x11
{

platform::platform()
    : input::platform()
{
#if HAVE_X11_XINPUT
    if (!qEnvironmentVariableIsSet("KWIN_NO_XI2")) {
        xinput.reset(new xinput_integration(QX11Info::display(), this));
        xinput->init();
        if (!xinput->hasXinput()) {
            xinput.reset();
        } else {
            QObject::connect(kwinApp(),
                             &Application::startup_finished,
                             xinput.get(),
                             &xinput_integration::startListening);
        }
    }
#endif

    redirect = std::make_unique<input::x11::redirect>(*this);
    create_cursor();
}

platform::~platform() = default;

void platform::setup_action_for_global_accel(QAction* action)
{
    QObject::connect(action, &QAction::triggered, this, [action] {
        auto timestamp = action->property("org.kde.kglobalaccel.activationTimestamp");
        bool ok = false;
        auto const time = timestamp.toULongLong(&ok);
        if (ok) {
            kwinApp()->setX11Time(time);
        }
    });
}

#if HAVE_X11_XINPUT
void platform::create_cursor()
{
    auto const is_xinput_avail = xinput != nullptr;
    cursor = std::make_unique<x11::cursor>(is_xinput_avail);

    if (is_xinput_avail) {
        xinput->setCursor(static_cast<x11::cursor*>(cursor.get()));

        xkb.setConfig(kwinApp()->kxkbConfig());
        xkb.reconfigure();
    }
}
#else
void platform::create_cursor()
{
    cursor = std::make_unique<x11::cursor>(false);
}
#endif

void platform::start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                                  QByteArray const& cursorName)
{
    if (!window_sel) {
        window_sel.reset(new window_selector);
    }
    window_sel->start(callback, cursorName);
}

void platform::start_interactive_position_selection(std::function<void(QPoint const&)> callback)
{
    if (!window_sel) {
        window_sel.reset(new window_selector);
    }
    window_sel->start(callback);
}

}
