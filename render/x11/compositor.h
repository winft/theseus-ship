/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "overlay_window.h"
#include "render/compositor.h"

#include <QObject>
#include <QRegion>
#include <QTimer>
#include <deque>
#include <memory>

namespace KWin
{
class Scene;
class Toplevel;

namespace render::x11
{

class KWIN_EXPORT compositor : public render::compositor
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

    compositor(render::platform& platform);
    static compositor* self();

    void schedule_repaint();
    void schedule_repaint(Toplevel* window) override;

    void toggleCompositing() override;

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

    void reinitialize() override;

    void addRepaint(QRegion const& region) override;
    void configChanged() override;

    /**
     * Checks whether @p w is the Scene's overlay window.
     */
    bool checkForOverlayWindow(WId w) const;

    void updateClientCompositeBlocking(Toplevel* window);

    /**
     * @brief The overlay window used by the backend, if any.
     */
    x11::overlay_window* overlay_window{nullptr};

protected:
    void start() override;
    render::scene* create_scene(QVector<CompositingType> const& support) override;
    std::deque<Toplevel*> performCompositing() override;

private:
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
}
