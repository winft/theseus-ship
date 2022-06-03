/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QBasicTimer>
#include <QObject>
#include <QRegion>
#include <QTimer>
#include <deque>
#include <map>
#include <memory>

namespace KWin
{
class Toplevel;

namespace win
{
class space;
}

namespace render
{

namespace dbus
{
class compositing;
}

namespace x11
{
class compositor_selection_owner;
}

class cursor;
class effects_handler_impl;
class platform;
class scene;

struct compositor_x11_integration {
    std::function<bool(xcb_window_t)> is_overlay_window;
    std::function<void(Toplevel*)> update_blocking;
};

class KWIN_EXPORT compositor : public QObject
{
    Q_OBJECT
public:
    enum class State {
        On = 0,
        Off,
        Starting,
        Stopping,
    };

    explicit compositor(render::platform& platform);
    ~compositor() override;

    virtual void start(win::space& space) = 0;

    // when adding repaints caused by a window, you probably want to use
    // either Toplevel::addRepaint() or Toplevel::addWorkspaceRepaint()
    virtual void addRepaint(QRegion const& region);
    void addRepaintFull();

    /**
     * Schedules a new repaint if no repaint is currently scheduled. Tries to optimize by only
     * repainting outputs that the visible bounds of @arg window intersect with.
     */
    virtual void schedule_repaint(Toplevel* window);
    virtual void schedule_frame_callback(Toplevel* window);

    /**
     * Notifies the compositor that SwapBuffers() is about to be called.
     * Rendering of the next frame will be deferred until bufferSwapComplete()
     * is called.
     */
    void aboutToSwapBuffers();

    /**
     * Notifies the compositor that a pending buffer swap has completed.
     */
    virtual void bufferSwapComplete(bool present = true);

    /**
     * Toggles compositing, that is if the Compositor is suspended it will be resumed
     * and if the Compositor is active it will be suspended.
     * Invoked by keybinding (shortcut default: Shift + Alt + F12).
     */
    virtual void toggleCompositing() = 0;

    /**
     * Re-initializes the Compositor completely.
     * Connected to the D-Bus signal org.kde.KWin /KWin reinitCompositing
     */
    virtual void reinitialize();

    /**
     * Whether the Compositor is active. That is a Scene is present and the Compositor is
     * not shutting down itself.
     */
    bool isActive();

    static bool compositing();

    // for delayed supportproperty management of effects
    void keepSupportProperty(xcb_atom_t atom);
    void removeSupportProperty(xcb_atom_t atom);

    std::unique_ptr<render::scene> scene;
    std::unique_ptr<render::effects_handler_impl> effects;

    // TODO(romangg): Only relevant for Wayland. Put in child class.
    std::unique_ptr<cursor> software_cursor;
    compositor_x11_integration x11_integration;

    render::platform& platform;
    win::space* space{nullptr};

Q_SIGNALS:
    void compositingToggled(bool active);
    void aboutToDestroy();
    void aboutToToggleCompositing();

protected:
    void timerEvent(QTimerEvent* te) override;
    void stop(bool on_shutdown);
    void start_scene();
    void setupX11Support();
    virtual std::unique_ptr<render::scene> create_scene() = 0;

    virtual void performCompositing() = 0;
    void update_paint_periods(int64_t duration);
    void retard_next_composition();
    void setCompositeTimer();

    virtual void configChanged();

    void destroyCompositorSelection();

    State m_state{State::Off};
    x11::compositor_selection_owner* m_selectionOwner{nullptr};
    QRegion repaints_region;
    QBasicTimer compositeTimer;
    qint64 m_delay{0};
    bool m_bufferSwapPending{false};
    std::unique_ptr<dbus::compositing> dbus;

private:
    void claimCompositorSelection();
    int refreshRate() const;

    void deleteUnusedSupportProperties();

    /**
     * The current refresh cycle length. In the future this should be per output on Wayland.
     *
     * @return refresh cycle length in nanoseconds.
     */
    qint64 refreshLength() const;

    QList<xcb_atom_t> m_unusedSupportProperties;
    QTimer m_unusedSupportPropertyTimer;

    // Compositing delay (in ns).
    qint64 m_lastPaintDurations[2]{0};
    int m_paintPeriods{0};
};

}
}
