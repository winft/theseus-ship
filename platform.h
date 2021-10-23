/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_PLATFORM_H
#define KWIN_PLATFORM_H
#include <kwin_export.h>
#include <kwinglobals.h>
#include <epoxy/egl.h>
#include "input/redirect.h"

#include <QImage>
#include <QObject>

#include <functional>
#include <memory>

class QAction;

namespace Wrapland
{
namespace Server
{
class Output;
class OutputConfigurationV1;
}
}

namespace KWin
{
namespace ColorCorrect {
class Manager;
}

class AbstractOutput;
class AbstractWaylandOutput;
class Edge;
class OverlayWindow;
class OpenGLBackend;
class Outline;
class OutlineVisual;
class QPainterBackend;
class Scene;
class Screens;
class ScreenEdges;
class Toplevel;

namespace Decoration
{
class Renderer;
class DecoratedClientImpl;
}

namespace render
{
class compositor;
}

class KWIN_EXPORT Outputs : public QVector<AbstractOutput*>
{
public:
    Outputs(){};
    template <typename T>
    Outputs(const QVector<T> &other) {
        resize(other.size());
        std::copy(other.constBegin(), other.constEnd(), begin());
    }
};

class KWIN_EXPORT Platform : public QObject
{
    Q_OBJECT
public:
    ~Platform() override;

    virtual OpenGLBackend *createOpenGLBackend();
    virtual QPainterBackend *createQPainterBackend();

    /**
     * Allows the platform to create a platform specific screen edge.
     * The default implementation creates a Edge.
     */
    virtual Edge *createScreenEdge(ScreenEdges *parent);
    virtual void warpPointer(const QPointF &globalPos);
    /**
     * Whether our Compositing EGL display allows a surface less context
     * so that a sharing context could be created.
     */
    virtual bool supportsQpaContext() const;
    /**
     * The EGLDisplay used by the compositing scene.
     */
    EGLDisplay sceneEglDisplay() const;
    void setSceneEglDisplay(EGLDisplay display);
    /**
     * The EGLContext used by the compositing scene.
     */
    virtual EGLContext sceneEglContext() const {
        return m_context;
    }
    /**
     * Sets the @p context used by the compositing scene.
     */
    void setSceneEglContext(EGLContext context) {
        m_context = context;
    }
    /**
     * The first (in case of multiple) EGLSurface used by the compositing scene.
     */
    EGLSurface sceneEglSurface() const {
        return m_surface;
    }
    /**
     * Sets the first @p surface used by the compositing scene.
     * @see sceneEglSurface
     */
    void setSceneEglSurface(EGLSurface surface) {
        m_surface = surface;
    }

    /**
     * The EglConfig used by the compositing scene.
     */
    EGLConfig sceneEglConfig() const {
        return m_eglConfig;
    }
    /**
     * Sets the @p config used by the compositing scene.
     * @see sceneEglConfig
     */
    void setSceneEglConfig(EGLConfig config) {
        m_eglConfig = config;
    }

    /**
     * Implementing subclasses should provide a size in case the backend represents
     * a basic screen and uses the BasicScreens.
     *
     * Base implementation returns an invalid size.
     */
    virtual QSize screenSize() const;
    /**
     * Implementing subclasses should provide all geometries in case the backend represents
     * a basic screen and uses the BasicScreens.
     *
     * Base implementation returns one QRect positioned at 0/0 with screenSize() as size.
     */
    virtual QVector<QRect> screenGeometries() const;

    /**
     * Implementing subclasses should provide all geometries in case the backend represents
     * a basic screen and uses the BasicScreens.
     *
     * Base implementation returns a screen with a scale of 1.
     */
    virtual QVector<qreal> screenScales() const;

    /**
     * Whether the Platform requires compositing for rendering.
     * Default implementation returns @c true. If the implementing Platform allows to be used
     * without compositing (e.g. rendering is done by the windowing system), re-implement this method.
     */
    virtual bool requiresCompositing() const;
    /**
     * Whether Compositing is possible in the Platform.
     * Returning @c false in this method makes only sense if requiresCompositing returns @c false.
     *
     * The default implementation returns @c true.
     * @see requiresCompositing
     */
    virtual bool compositingPossible() const;
    /**
     * Returns a user facing text explaining why compositing is not possible in case
     * compositingPossible returns @c false.
     *
     * The default implementation returns an empty string.
     * @see compositingPossible
     */
    virtual QString compositingNotPossibleReason() const;
    /**
     * Whether OpenGL compositing is broken.
     * The Platform can implement this method if it is able to detect whether OpenGL compositing
     * broke (e.g. triggered a crash in a previous run).
     *
     * Default implementation returns @c false.
     * @see createOpenGLSafePoint
     */
    virtual bool openGLCompositingIsBroken() const;
    /**
     * This method is invoked before and after creating the OpenGL rendering Scene.
     * An implementing Platform can use it to detect crashes triggered by the OpenGL implementation.
     * This can be used for openGLCompositingIsBroken.
     *
     * The default implementation does nothing.
     * @see openGLCompositingIsBroken.
     */
    virtual void createOpenGLSafePoint(OpenGLSafePoint safePoint);

    /**
     * Platform specific preparation for an @p action which is used for KGlobalAccel.
     *
     * A platform might need to do preparation for an @p action before
     * it can be used with KGlobalAccel.
     *
     * Code using KGlobalAccel should invoke this method for the @p action
     * prior to setting up any shortcuts and connections.
     *
     * The default implementation does nothing.
     *
     * @param action The action which will be used with KGlobalAccel.
     * @since 5.10
     */
    virtual void setupActionForGlobalAccel(QAction *action);

    void setInitialWindowSize(const QSize &size) {
        m_initialWindowSize = size;
    }
    void setDeviceIdentifier(const QByteArray &identifier) {
        m_deviceIdentifier = identifier;
    }
    bool supportsPointerWarping() const {
        return m_pointerWarping;
    }
    int initialOutputCount() const {
        return m_initialOutputCount;
    }
    void setInitialOutputCount(int count) {
        m_initialOutputCount = count;
    }
    qreal initialOutputScale() const {
        return m_initialOutputScale;
    }
    void setInitialOutputScale(qreal scale) {
        m_initialOutputScale = scale;
    }

    /**
     * Creates the OverlayWindow required for X11 based compositors.
     * Default implementation returns @c nullptr.
     */
    virtual OverlayWindow *createOverlayWindow();

    /**
     * Queries the current X11 time stamp of the X server.
     */
    void updateXTime();

    /**
     * Creates the OutlineVisual for the given @p outline.
     * Default implementation creates an OutlineVisual suited for composited usage.
     */
    virtual OutlineVisual *createOutline(Outline *outline);

    /**
     * Creates the Decoration::Renderer for the given @p client.
     *
     * The default implementation creates a Renderer suited for the Compositor, @c nullptr if there is no Compositor.
     */
    virtual Decoration::Renderer *createDecorationRenderer(Decoration::DecoratedClientImpl *client);

    /**
     * Platform specific way to invert the screen.
     * Default implementation invokes the invert effect
     */
    virtual void invertScreen();

    /**
     * Default implementation creates an EffectsHandlerImp;
     */
    virtual void createEffectsHandler(render::compositor *compositor, Scene *scene);

    /**
     * The CompositingTypes supported by the Platform.
     * The first item should be the most preferred one.
     * @since 5.11
     */
    virtual QVector<CompositingType> supportedCompositors() const = 0;

    /**
     * Whether gamma control is supported by the backend.
     * @since 5.12
     */
    bool supportsGammaControl() const {
        return m_supportsGammaControl;
    }

    ColorCorrect::Manager *colorCorrectManager() {
        return m_colorCorrect;
    }

    // outputs with connections (org_kde_kwin_outputdevice)
    virtual Outputs outputs() const {
        return Outputs();
    }
    // actively compositing outputs (wl_output)
    virtual Outputs enabledOutputs() const {
        return Outputs();
    }

    /**
     * A string of information to include in kwin debug output
     * It should not be translated.
     *
     * The base implementation prints the name.
     * @since 5.12
     */
    virtual QString supportInformation() const;

    /**
     * The compositor plugin which got selected from @ref supportedCompositors.
     * Prior to selecting a compositor this returns @c NoCompositing.
     *
     * This method allows the platforms to limit the offerings in @ref supportedCompositors
     * in case they do not support runtime compositor switching
     */
    CompositingType selectedCompositor() const
    {
        return m_selectedCompositor;
    }
    /**
     * Used by Compositor to set the used compositor.
     */
    void setSelectedCompositor(CompositingType type)
    {
        m_selectedCompositor = type;
    }

    QSize initialWindowSize() const {
        return m_initialWindowSize;
    }

    virtual clockid_t clockId() const;
    QByteArray deviceIdentifier() const {
        return m_deviceIdentifier;
    }
    void repaint(const QRect &rect);

Q_SIGNALS:
    /**
     * Emitted by backends using a one screen (nested window) approach and when the size of that changes.
     */
    void screenSizeChanged();

    void output_added(AbstractOutput* output);
    void output_removed(AbstractOutput* output);

protected:
    Platform();
    void setSupportsPointerWarping(bool set) {
        m_pointerWarping = set;
    }
    void setSupportsGammaControl(bool set) {
        m_supportsGammaControl = set;
    }

private:
    QSize m_initialWindowSize;
    QByteArray m_deviceIdentifier;
    bool m_pointerWarping = false;
    int m_initialOutputCount = 1;
    qreal m_initialOutputScale = 1;
    EGLDisplay m_eglDisplay;
    EGLConfig m_eglConfig = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    ColorCorrect::Manager *m_colorCorrect = nullptr;
    bool m_supportsGammaControl = false;
    CompositingType m_selectedCompositor = NoCompositing;
};

}

Q_DECLARE_INTERFACE(KWin::Platform, "org.kde.kwin.Platform")

#endif
