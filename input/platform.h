/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xkb.h"

#include <kwin_export.h>

#include <KSharedConfig>
#include <QObject>
#include <memory>
#include <vector>

namespace KWin::input
{

namespace dbus
{
class device_manager;
}

class cursor;
class keyboard;
class pointer;
class redirect;
class switch_device;
class touch;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;
    std::vector<switch_device*> switches;
    std::vector<touch*> touchs;

    std::unique_ptr<input::redirect> redirect;
    std::unique_ptr<input::cursor> cursor;

    std::unique_ptr<dbus::device_manager> dbus;
    KSharedConfigPtr config;

    platform();
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() override;

    /**
     * Starts an interactive window selection process.
     *
     * Once the user selected a window the @p callback is invoked with the selected Toplevel as
     * argument. In case the user cancels the interactive window selection or selecting a window is
     * currently not possible (e.g. screen locked) the @p callback is invoked with a @c nullptr
     * argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor unless
     * @p cursorName is provided. The argument @p cursorName is a QByteArray instead of
     * Qt::CursorShape to support the "pirate" cursor for kill window which is not wrapped by
     * Qt::CursorShape.
     *
     * @param callback The function to invoke once the interactive window selection ends
     * @param cursorName The optional name of the cursor shape to use, default is crosshair
     */
    virtual void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                                    QByteArray const& cursorName = QByteArray())
        = 0;

    /**
     * Starts an interactive position selection process.
     *
     * Once the user selected a position on the screen the @p callback is invoked with
     * the selected point as argument. In case the user cancels the interactive position selection
     * or selecting a position is currently not possible (e.g. screen locked) the @p callback
     * is invoked with a point at @c -1 as x and y argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor.
     *
     * @param callback The function to invoke once the interactive position selection ends
     */
    virtual void start_interactive_position_selection(std::function<void(QPoint const&)> callback)
        = 0;

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

KWIN_EXPORT void add_redirect(platform* platform, std::unique_ptr<redirect> redirect);

}
