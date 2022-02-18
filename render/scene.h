/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "types.h"

#include "kwineffects.h"

#include <QMatrix4x4>
#include <deque>
#include <memory>
#include <xcb/render.h>

class QOpenGLFramebufferObject;

namespace Wrapland::Server
{
class Buffer;
}

namespace KWin
{

class Toplevel;

namespace base
{
class output;
}

namespace Decoration
{
class DecoratedClientImpl;
class Renderer;
}

namespace render
{
class compositor;
class effect_frame;
class effect_frame_impl;
class effects_window_impl;
class shadow;
class window;
class window_pixmap;

struct scene_windowing_integration {
    std::function<void(void)> handle_viewport_limits_alarm;
};

// The base class for compositing backends.
class KWIN_EXPORT scene : public QObject
{
    Q_OBJECT
public:
    explicit scene(render::compositor& compositor);
    ~scene() override = 0;

    // Returns true if the ctor failed to properly initialize.
    virtual bool initFailed() const = 0;
    virtual CompositingType compositingType() const = 0;

    virtual bool hasPendingFlush() const
    {
        return false;
    }

    /**
     * The entry point for the main part of the painting pass. Repaints the given screen areas.
     *
     * @param damage is the area that needs to be repaint
     * @param windows provides the stacking order
     * @return the elapsed time in ns
     */
    virtual int64_t paint(QRegion damage,
                          std::deque<Toplevel*> const& windows,
                          std::chrono::milliseconds presentTime);
    virtual int64_t paint_output(base::output* output,
                                 QRegion damage,
                                 std::deque<Toplevel*> const& windows,
                                 std::chrono::milliseconds presentTime);

    virtual std::unique_ptr<render::window> createWindow(Toplevel* toplevel) = 0;
    /**
     * Removes the Toplevel from the scene.
     *
     * @param toplevel The window to be removed.
     * @note You can remove a toplevel from the scene only once.
     */
    void removeToplevel(Toplevel* toplevel);

    /**
     * @brief Creates the scene backend of an effect frame.
     *
     * @param frame The effect frame this effect_frame belongs to.
     */
    virtual effect_frame* createEffectFrame(effect_frame_impl* frame) = 0;
    /**
     * @brief Creates the scene specific shadow subclass.
     *
     * An implementing class has to create a proper instance. It is not allowed to
     * return @c null.
     *
     * @param toplevel The Toplevel for which the Shadow needs to be created.
     */
    virtual shadow* createShadow(Toplevel* toplevel) = 0;
    /**
     * Method invoked when the screen geometry is changed.
     * Reimplementing classes should also invoke the parent method
     * as it takes care of resizing the overlay window.
     * @param size The new screen geometry size
     */
    virtual void handle_screen_geometry_change(QSize const& size) = 0;

    // there's nothing to paint (adjust time_diff later)
    virtual void idle();
    virtual bool hasSwapEvent() const;

    virtual bool makeOpenGLContextCurrent();
    virtual void doneOpenGLContextCurrent();
    virtual bool supportsSurfacelessContext() const;

    virtual QMatrix4x4 screenProjectionMatrix() const;

    virtual void triggerFence();

    virtual Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl*) = 0;

    /**
     * Whether the scene is able to drive animations.
     * This is used as a hint to the effects system which effects can be supported.
     * If the scene performs software rendering it is supposed to return @c false,
     * if rendering is hardware accelerated it should return @c true.
     */
    virtual bool animationsSupported() const = 0;

    /**
     * The render buffer used by an XRender based compositor scene.
     * Default implementation returns XCB_RENDER_PICTURE_NONE
     */
    virtual xcb_render_picture_t xrenderBufferPicture() const;

    /**
     * The QPainter used by a QPainter based compositor scene.
     * Default implementation returns @c nullptr;
     */
    virtual QPainter* scenePainter() const;

    /**
     * The backend specific extensions (e.g. EGL/GLX extensions).
     *
     * Not the OpenGL (ES) extension!
     *
     * Default implementation returns empty list
     */
    virtual QVector<QByteArray> openGLPlatformInterfaceExtensions() const;

    QHash<Toplevel*, window*> m_windows;
    render::compositor& compositor;
    scene_windowing_integration windowing_integration;

Q_SIGNALS:
    void frameRendered();
    void resetCompositing();

public Q_SLOTS:
    // shape/size of a window changed
    void windowGeometryShapeChanged(KWin::Toplevel* c);
    // a window has been closed
    void windowClosed(KWin::Toplevel* toplevel, KWin::Toplevel* deleted);

protected:
    void createStackingOrder(std::deque<Toplevel*> const& toplevels);
    void clearStackingOrder();
    // shared implementation, starts painting the screen
    void paintScreen(paint_type& mask,
                     const QRegion& damage,
                     const QRegion& repaint,
                     QRegion* updateRegion,
                     QRegion* validRegion,
                     std::chrono::milliseconds presentTime,
                     const QMatrix4x4& projection = QMatrix4x4());
    // Render cursor texture in case hardware cursor is disabled/non-applicable
    virtual void paintCursor() = 0;
    friend class effects_handler_impl;
    // called after all effects had their paintScreen() called
    void finalPaintScreen(paint_type mask, QRegion region, ScreenPaintData& data);
    // shared implementation of painting the screen in the generic
    // (unoptimized) way
    virtual void paintGenericScreen(paint_type mask, ScreenPaintData data);
    // shared implementation of painting the screen in an optimized way
    virtual void paintSimpleScreen(paint_type mask, QRegion region);
    // paint the background (not the desktop background - the whole background)
    virtual void paintBackground(QRegion region) = 0;
    // called after all effects had their paintWindow() called
    void finalPaintWindow(effects_window_impl* w,
                          paint_type mask,
                          QRegion region,
                          WindowPaintData& data);
    // shared implementation, starts painting the window
    virtual void paintWindow(window* w, paint_type mask, QRegion region, WindowQuadList quads);
    // called after all effects had their drawWindow() called
    virtual void
    finalDrawWindow(effects_window_impl* w, paint_type mask, QRegion region, WindowPaintData& data);
    // let the scene decide whether it's better to paint more of the screen, eg. in order to allow a
    // buffer swap the default is NOOP
    virtual void extendPaintRegion(QRegion& region, bool opaqueFullscreen);
    virtual void
    paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data);

    virtual void paintEffectQuickView(EffectQuickView* w) = 0;

    // saved data for 2nd pass of optimized screen painting
    struct Phase2Data {
        render::window* window = nullptr;
        QRegion region;
        QRegion clip;
        paint_type mask{paint_type::none};
        WindowQuadList quads;
    };
    // The region which actually has been painted by paintScreen() and should be
    // copied from the buffer to the screen. I.e. the region returned from scene::paintScreen().
    // Since prePaintWindow() can extend areas to paint, these changes would have to propagate
    // up all the way from paintSimpleScreen() up to paintScreen(), so save them here rather
    // than propagate them up in arguments.
    QRegion painted_region;
    // Additional damage that needs to be repaired to bring a reused back buffer up to date
    QRegion repaint_region;
    // The dirty region before it was unioned with repaint_region
    QRegion damaged_region;

    /**
     * The output currently being repainted. Only relevant for per-output painting.
     */
    base::output* repaint_output{nullptr};

private:
    void paintWindowThumbnails(window* w,
                               QRegion region,
                               qreal opacity,
                               qreal brightness,
                               qreal saturation);
    void paintDesktopThumbnails(window* w);
    std::chrono::milliseconds m_expectedPresentTimestamp = std::chrono::milliseconds::zero();
    // windows in their stacking order
    QVector<window*> stacking_order;
};

}
}
