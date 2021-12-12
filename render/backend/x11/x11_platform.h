/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_X11_PLATFORM_H
#define KWIN_X11_PLATFORM_H
#include "platform.h"

#include "base/x11/platform.h"

#include <kwin_export.h>

#include <QObject>

#include <memory>

#include <X11/Xlib-xcb.h>
#include <fixx11h.h>

namespace KWin
{

namespace base::x11
{
class event_filter;
}

namespace render::backend::x11
{
class X11Output;

class KWIN_EXPORT X11StandalonePlatform : public Platform
{
    Q_OBJECT
public:
    X11StandalonePlatform(base::x11::platform& base);
    ~X11StandalonePlatform() override;

    void init();

    gl::backend* createOpenGLBackend(render::compositor* compositor) override;
    bool requiresCompositing() const override;
    bool compositingPossible() const override;
    QString compositingNotPossibleReason() const override;
    bool openGLCompositingIsBroken() const override;
    void createOpenGLSafePoint(OpenGLSafePoint safePoint) override;

    void setupActionForGlobalAccel(QAction* action) override;

    render::x11::outline_visual* createOutline(render::x11::outline* outline) override;
    Decoration::Renderer*
    createDecorationRenderer(Decoration::DecoratedClientImpl* client) override;

    void invertScreen() override;

    void createEffectsHandler(render::compositor* compositor, render::scene* scene) override;
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
    base::x11::platform& base;

    QScopedPointer<base::x11::event_filter> m_randrFilter;

    QVector<X11Output*> m_outputs;
};

}
}

#endif
