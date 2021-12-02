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

#include "kwineffects.h"
#include "toplevel.h"
#include "utils.h"

#include <QElapsedTimer>
#include <QMatrix4x4>

#include <deque>
#include <memory>

class QOpenGLFramebufferObject;

namespace Wrapland::Server
{
class Buffer;
}

namespace KWin
{

namespace base
{
class output;
}
namespace Decoration
{
class DecoratedClientImpl;
class Renderer;
}

class Deleted;

namespace render
{
class effect_frame;
class effect_frame_impl;
class effects_window_impl;
class shadow;
class window;
class window_pixmap;

enum class image_filter_type {
    fast,
    good,
};

enum class paint_type {
    none = 0,

    // Window (or at least part of it) will be painted opaque.
    window_opaque = 1 << 0,

    // Window (or at least part of it) will be painted translucent.
    window_translucent = 1 << 1,

    // Window will be painted with transformed geometry.
    window_transformed = 1 << 2,

    // Paint only a region of the screen (can be optimized, cannot
    // be used together with TRANSFORMED flags).
    screen_region = 1 << 3,

    // Whole screen will be painted with transformed geometry.
    screen_transformed = 1 << 4,

    // At least one window will be painted with transformed geometry.
    screen_with_transformed_windows = 1 << 5,

    // Clear whole background as the very first step, without optimizing it
    screen_background_first = 1 << 6,

    // decoration_only = 1 << 7 has been removed

    // Window will be painted with a lanczos filter.
    window_lanczos = 1 << 8

    // screen_with_transformed_windows_without_full_repaints = 1 << 9 has been removed
};

// The base class for compositing backends.
class KWIN_EXPORT scene : public QObject
{
    Q_OBJECT
public:
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

    /**
     * Adds the Toplevel to the scene.
     *
     * If the toplevel gets deleted, then the scene will try automatically
     * to re-bind an underlying scene window to the corresponding Deleted.
     *
     * @param toplevel The window to be added.
     * @note You can add a toplevel to scene only once.
     */
    void addToplevel(Toplevel* toplevel);

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
     * The render buffer used by a QPainter based compositor.
     * Default implementation returns @c nullptr.
     */
    virtual QImage* qpainterRenderBuffer() const;

    /**
     * The backend specific extensions (e.g. EGL/GLX extensions).
     *
     * Not the OpenGL (ES) extension!
     *
     * Default implementation returns empty list
     */
    virtual QVector<QByteArray> openGLPlatformInterfaceExtensions() const;

Q_SIGNALS:
    void frameRendered();
    void resetCompositing();

public Q_SLOTS:
    // shape/size of a window changed
    void windowGeometryShapeChanged(KWin::Toplevel* c);
    // a window has been closed
    void windowClosed(KWin::Toplevel* toplevel, KWin::Toplevel* deleted);

protected:
    virtual window* createWindow(Toplevel* toplevel) = 0;
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
    QHash<Toplevel*, window*> m_windows;
    // windows in their stacking order
    QVector<window*> stacking_order;
};

enum class window_paint_disable_type {
    none = 0,

    // Window will not be painted for an unspecified reason
    unspecified = 1 << 0,

    // Window will not be painted because it is deleted
    by_delete = 1 << 1,

    // Window will not be painted because of which desktop it's on
    by_desktop = 1 << 2,

    // Window will not be painted because it is minimized
    by_minimize = 1 << 3,

    // Window will not be painted because it's not on the current activity
    by_activity = 1 << 5, /// Deprecated
};

// The base class for windows representations in composite backends
class KWIN_EXPORT window
{
public:
    window(Toplevel* c);
    virtual ~window();
    uint32_t id() const;
    // perform the actual painting of the window
    virtual void performPaint(paint_type mask, QRegion region, WindowPaintData data) = 0;
    // do any cleanup needed when the window's composite pixmap is discarded
    void discardPixmap();
    void updatePixmap();
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    QRect geometry() const;
    QPoint pos() const;
    QSize size() const;
    QRect rect() const;
    // access to the internal window class
    // TODO eventually get rid of this
    Toplevel* get_window() const;
    // should the window be painted
    bool isPaintingEnabled() const;
    void resetPaintingEnabled();
    void enablePainting(window_paint_disable_type reason);
    void disablePainting(window_paint_disable_type reason);
    // is the window visible at all
    bool isVisible() const;
    // is the window fully opaque
    bool isOpaque() const;
    QRegion decorationShape() const;
    QPoint bufferOffset() const;
    void updateToplevel(Toplevel* c);
    // creates initial quad list for the window
    WindowQuadList buildQuads(bool force = false) const;
    void updateShadow(render::shadow* shadow);
    render::shadow const* shadow() const;
    render::shadow* shadow();
    void referencePreviousPixmap();
    void unreferencePreviousPixmap();
    void invalidateQuadsCache();

protected:
    WindowQuadList
    makeDecorationQuads(const QRect* rects, const QRegion& region, qreal textureScale = 1.0) const;
    WindowQuadList makeContentsQuads(int id, QPoint const& offset = QPoint()) const;
    /**
     * @brief Returns the window pixmap for this Window.
     *
     * If the window pixmap does not yet exist, this method will invoke createWindowPixmap.
     * If the window pixmap is not valid it tries to create it, in case this succeeds the
     * window pixmap is returned. In case it fails, the previous (and still valid) window pixmap is
     * returned.
     *
     * @note This method can return @c NULL as there might neither be a valid previous nor current
     * window pixmap around.
     *
     * The window pixmap gets casted to the type passed in as a template parameter. That way this
     * class does not need to know the actual window pixmap subclass used by the concrete scene
     * implementations.
     *
     * @return The window_pixmap casted to T* or @c NULL if there is no valid window pixmap.
     */
    template<typename T>
    T* windowPixmap();
    template<typename T>
    T* previousWindowPixmap();
    /**
     * @brief Factory method to create a window_pixmap.
     *
     * The inheriting classes need to implement this method to create a new instance of their
     * window_pixmap subclass.
     * @note Do not use window_pixmap::create on the created instance. The scene will take care of
     * that.
     */
    virtual window_pixmap* createWindowPixmap() = 0;
    Toplevel* toplevel;
    image_filter_type filter;
    render::shadow* m_shadow;

private:
    QScopedPointer<window_pixmap> m_currentPixmap;
    QScopedPointer<window_pixmap> m_previousPixmap;
    int m_referencePixmapCounter;
    window_paint_disable_type disable_painting{window_paint_disable_type::none};
    mutable QScopedPointer<WindowQuadList> cached_quad_list;
    uint32_t const m_id;
    Q_DISABLE_COPY(window)
};

/**
 * @brief Wrapper for a pixmap of the window.
 *
 * This class encapsulates the functionality to get the pixmap for a window. When initialized the
 * pixmap is not yet mapped to the window and isValid will return @c false. The pixmap mapping to
 * the window can be established through @ref create. If it succeeds isValid will return @c true,
 * otherwise it will keep in the non valid state and it can be tried to create the pixmap mapping
 * again (e.g. in the next frame).
 *
 * This class is not intended to be updated when the pixmap is no longer valid due to e.g. resizing
 * the window. Instead a new instance of this class should be instantiated. The idea behind this is
 * that a valid pixmap does not get destroyed, but can continue to be used. To indicate that a newer
 * pixmap should in generally be around, one can use markAsDiscarded.
 *
 * This class is intended to be inherited for the needs of the compositor backends which need
 * further mapping from the native pixmap to the respective rendering format.
 */
class KWIN_EXPORT window_pixmap
{
public:
    virtual ~window_pixmap();
    /**
     * @brief Tries to create the mapping between the Window and the pixmap.
     *
     * In case this method succeeds in creating the pixmap for the window, isValid will return @c
     * true otherwise
     * @c false.
     *
     * Inheriting classes should re-implement this method in case they need to add further
     * functionality for mapping the native pixmap to the rendering format.
     */
    virtual void create();
    /**
     * @return @c true if the pixmap has been created and is valid, @c false otherwise
     */
    virtual bool isValid() const;
    /**
     * @return The native X11 pixmap handle
     */
    xcb_pixmap_t pixmap() const;
    /**
     * @return The Wayland Buffer for this window pixmap.
     */
    Wrapland::Server::Buffer* buffer() const;
    const QSharedPointer<QOpenGLFramebufferObject>& fbo() const;
    QImage internalImage() const;
    /**
     * @brief Whether this window pixmap is considered as discarded. This means the window has
     * changed in a way that a new window pixmap should have been created already.
     *
     * @return @c true if this window pixmap is considered as discarded, @c false otherwise.
     * @see markAsDiscarded
     */
    bool isDiscarded() const;
    /**
     * @brief Marks this window pixmap as discarded. From now on isDiscarded will return @c true.
     * This method should only be used by the Window when it changes in a way that a new pixmap is
     * required.
     *
     * @see isDiscarded
     */
    void markAsDiscarded();
    /**
     * The size of the pixmap.
     */
    const QSize& size() const;
    /**
     * The geometry of the Client's content inside the pixmap. In case of a decorated Client the
     * pixmap also contains the decoration which is not rendered into this pixmap, though. This
     * contentsRect tells where inside the complete pixmap the real content is.
     */
    const QRect& contentsRect() const;
    /**
     * @brief Returns the Toplevel this window pixmap belongs to.
     * Note: the Toplevel can change over the lifetime of the window pixmap in case the Toplevel is
     * copied to Deleted.
     */
    Toplevel* toplevel() const;

    /**
     * @returns the surface this window pixmap references, might be @c null.
     */
    Wrapland::Server::Surface* surface() const;

protected:
    explicit window_pixmap(render::window* window);

    /**
     * Should be called by the implementing subclasses when the Wayland Buffer changed and needs
     * updating.
     */
    virtual void updateBuffer();

private:
    render::window* m_window;
    xcb_pixmap_t m_pixmap;
    QSize m_pixmapSize;
    bool m_discarded;
    QRect m_contentsRect;
    std::shared_ptr<Wrapland::Server::Buffer> m_buffer;
    QSharedPointer<QOpenGLFramebufferObject> m_fbo;
    QImage m_internalImage;
};

class KWIN_EXPORT effect_frame
{
public:
    effect_frame(effect_frame_impl* frame);
    virtual ~effect_frame();
    virtual void render(QRegion region, double opacity, double frameOpacity) = 0;
    virtual void free() = 0;
    virtual void freeIconFrame() = 0;
    virtual void freeTextFrame() = 0;
    virtual void freeSelection() = 0;
    virtual void crossFadeIcon() = 0;
    virtual void crossFadeText() = 0;

protected:
    effect_frame_impl* m_effectFrame;
};

inline int window::x() const
{
    return toplevel->pos().x();
}

inline int window::y() const
{
    return toplevel->pos().y();
}

inline int window::width() const
{
    return toplevel->size().width();
}

inline int window::height() const
{
    return toplevel->size().height();
}

inline QRect window::geometry() const
{
    return toplevel->frameGeometry();
}

inline QSize window::size() const
{
    return toplevel->size();
}

inline QPoint window::pos() const
{
    return toplevel->pos();
}

inline QRect window::rect() const
{
    return QRect(QPoint(), toplevel->size());
}

inline Toplevel* window::get_window() const
{
    return toplevel;
}

inline void window::updateToplevel(Toplevel* c)
{
    toplevel = c;
}

inline render::shadow const* window::shadow() const
{
    return m_shadow;
}

inline render::shadow* window::shadow()
{
    return m_shadow;
}

inline Wrapland::Server::Buffer* window_pixmap::buffer() const
{
    return m_buffer.get();
}

inline const QSharedPointer<QOpenGLFramebufferObject>& window_pixmap::fbo() const
{
    return m_fbo;
}

inline QImage window_pixmap::internalImage() const
{
    return m_internalImage;
}

template<typename T>
inline T* window::windowPixmap()
{
    if (m_currentPixmap.isNull()) {
        m_currentPixmap.reset(createWindowPixmap());
    }
    if (m_currentPixmap->isValid()) {
        return static_cast<T*>(m_currentPixmap.data());
    }
    m_currentPixmap->create();
    if (m_currentPixmap->isValid()) {
        return static_cast<T*>(m_currentPixmap.data());
    } else {
        return static_cast<T*>(m_previousPixmap.data());
    }
}

template<typename T>
inline T* window::previousWindowPixmap()
{
    return static_cast<T*>(m_previousPixmap.data());
}

inline Toplevel* window_pixmap::toplevel() const
{
    return m_window->get_window();
}

inline xcb_pixmap_t window_pixmap::pixmap() const
{
    return m_pixmap;
}

inline bool window_pixmap::isDiscarded() const
{
    return m_discarded;
}

inline void window_pixmap::markAsDiscarded()
{
    m_discarded = true;
    m_window->referencePreviousPixmap();
}

inline const QRect& window_pixmap::contentsRect() const
{
    return m_contentsRect;
}

inline const QSize& window_pixmap::size() const
{
    return m_pixmapSize;
}

}
}

ENUM_FLAGS(KWin::render::paint_type)
ENUM_FLAGS(KWin::render::window_paint_disable_type)
