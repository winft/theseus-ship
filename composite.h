/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwinglobals.h>

#include <QBasicTimer>
#include <QObject>
#include <QRegion>
#include <QTimer>

#include <deque>
#include <map>
#include <memory>

namespace KWin
{
class CompositorSelectionOwner;
class Scene;
class Toplevel;

class KWIN_EXPORT Compositor : public QObject
{
    Q_OBJECT
public:
    enum class State {
        On = 0,
        Off,
        Starting,
        Stopping,
    };

    ~Compositor() override;
    static Compositor* self();

    // when adding repaints caused by a window, you probably want to use
    // either Toplevel::addRepaint() or Toplevel::addWorkspaceRepaint()
    void addRepaint(QRect const& rect);
    void addRepaint(int x, int y, int w, int h);
    virtual void addRepaint(QRegion const& region);
    void addRepaintFull();

    /**
     * Schedules a new repaint for all outputs if no repaint is currently scheduled.
     */
    void schedule_repaint();

    /**
     * Schedules a new repaint if no repaint is currently scheduled. Tries to optimize by only
     * repainting outputs that the visible bounds of @arg window intersect with.
     */
    virtual void schedule_repaint(Toplevel* window);

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

    Scene* scene() const
    {
        return m_scene;
    }

    static bool compositing();

    // for delayed supportproperty management of effects
    void keepSupportProperty(xcb_atom_t atom);
    void removeSupportProperty(xcb_atom_t atom);

Q_SIGNALS:
    void compositingToggled(bool active);
    void aboutToDestroy();
    void aboutToToggleCompositing();
    void sceneCreated();

protected:
    explicit Compositor(QObject* parent = nullptr);
    void timerEvent(QTimerEvent* te) override;

    virtual void start() = 0;
    void stop();

    /**
     * @brief Prepares start.
     * @return bool @c true if start should be continued and @c if not.
     */
    bool setupStart();
    /**
     * Continues the startup after Scene And Workspace are created
     */
    void startupWithWorkspace();

    virtual std::deque<Toplevel*> performCompositing() = 0;
    void update_paint_periods(int64_t duration);
    void retard_next_composition();

    virtual void configChanged();

    void destroyCompositorSelection();

    State m_state;
    CompositorSelectionOwner* m_selectionOwner;
    QRegion repaints_region;
    QBasicTimer compositeTimer;
    qint64 m_delay;
    bool m_bufferSwapPending;

private:
    void claimCompositorSelection();
    int refreshRate() const;

    void setupX11Support();

    void setCompositeTimer();

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

    Scene* m_scene;
};

class KWIN_EXPORT X11Compositor : public Compositor
{
    Q_OBJECT
public:
    enum SuspendReason {
        NoReasonSuspend = 0,
        UserSuspend = 1 << 0,
        BlockRuleSuspend = 1 << 1,
        ScriptSuspend = 1 << 2,
        AllReasonSuspend = 0xff,
    };
    Q_DECLARE_FLAGS(SuspendReasons, SuspendReason)
    Q_ENUM(SuspendReason)
    Q_FLAG(SuspendReasons)

    static X11Compositor* create(QObject* parent = nullptr);

    /**
     * @brief Suspends the Compositor if it is currently active.
     *
     * Note: it is possible that the Compositor is not able to suspend. Use isActive to check
     * whether the Compositor has been suspended.
     *
     * @return void
     * @see resume
     * @see isActive
     */
    void suspend(SuspendReason reason);

    /**
     * @brief Resumes the Compositor if it is currently suspended.
     *
     * Note: it is possible that the Compositor cannot be resumed, that is there might be Clients
     * blocking the usage of Compositing or the Scene might be broken. Use isActive to check
     * whether the Compositor has been resumed. Also check isCompositingPossible and
     * isOpenGLBroken.
     *
     * Note: The starting of the Compositor can require some time and is partially done threaded.
     * After this method returns the setup may not have been completed.
     *
     * @return void
     * @see suspend
     * @see isActive
     * @see isCompositingPossible
     * @see isOpenGLBroken
     */
    void resume(SuspendReason reason);

    void toggleCompositing() override;
    void addRepaint(QRegion const& region) override;
    void reinitialize() override;

    void configChanged() override;

    /**
     * Checks whether @p w is the Scene's overlay window.
     */
    bool checkForOverlayWindow(WId w) const;

    /**
     * @returns Whether the Scene's Overlay X Window is visible.
     */
    bool isOverlayWindowVisible() const;

    void updateClientCompositeBlocking(Toplevel* window = nullptr);

    static X11Compositor* self();

protected:
    void start() override;
    std::deque<Toplevel*> performCompositing() override;

private:
    explicit X11Compositor(QObject* parent);

    void releaseCompositorSelection();
    bool prepare_composition(QRegion& repaints, std::deque<Toplevel*>& windows);
    void create_opengl_safepoint(OpenGLSafePoint safepoint);

    /**
     * Whether the Compositor is currently suspended, 8 bits encoding the reason
     */
    SuspendReasons m_suspended;
    QTimer m_releaseSelectionTimer;
    int m_framesToTestForSafety{3};
};

}
