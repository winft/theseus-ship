/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_X11_PLATFORM_H
#define KWIN_X11_PLATFORM_H
#include "platform.h"

#include <kwin_export.h>

#include <QObject>

#include <memory>

#include <X11/Xlib-xcb.h>
#include <fixx11h.h>

namespace KWin
{

namespace platform::x11
{
class event_filter;
}

namespace render::backend::x11
{
class WindowSelector;
class X11Output;

class KWIN_EXPORT X11StandalonePlatform : public Platform
{
    Q_OBJECT
public:
    X11StandalonePlatform(QObject* parent = nullptr);
    ~X11StandalonePlatform() override;

    void init();

    OpenGLBackend* createOpenGLBackend() override;
    Edge* createScreenEdge(ScreenEdges* parent) override;
    bool requiresCompositing() const override;
    bool compositingPossible() const override;
    QString compositingNotPossibleReason() const override;
    bool openGLCompositingIsBroken() const override;
    void createOpenGLSafePoint(OpenGLSafePoint safePoint) override;

    void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                         const QByteArray& cursorName = QByteArray()) override;
    void startInteractivePositionSelection(std::function<void(const QPoint&)> callback) override;

    void setupActionForGlobalAccel(QAction* action) override;

    OverlayWindow* createOverlayWindow() override;
    OutlineVisual* createOutline(Outline* outline) override;
    Decoration::Renderer*
    createDecorationRenderer(Decoration::DecoratedClientImpl* client) override;

    QSize screenSize() const override;
    void invertScreen() override;

    void createEffectsHandler(render::compositor* compositor, Scene* scene) override;
    QVector<CompositingType> supportedCompositors() const override;

    void initOutputs();
    void updateOutputs();

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

private:
    /**
     * Tests whether GLX is supported and returns @c true
     * in case KWin is compiled with OpenGL support and GLX
     * is available.
     *
     * If KWin is compiled with OpenGL ES or without OpenGL at
     * all, @c false is returned.
     * @returns @c true if GLX is available, @c false otherwise and if not build with OpenGL
     * support.
     */
    static bool hasGlx();

    template<typename T>
    void doUpdateOutputs();

    QThread* m_openGLFreezeProtectionThread = nullptr;
    QTimer* m_openGLFreezeProtection = nullptr;
    Display* m_x11Display;
    QScopedPointer<WindowSelector> m_windowSelector;
    QScopedPointer<platform::x11::event_filter> m_screenEdgesFilter;
    QScopedPointer<platform::x11::event_filter> m_randrFilter;

    QVector<X11Output*> m_outputs;
};

}
}

#endif
