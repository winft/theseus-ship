/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/platform.h"

#include "base/x11/platform.h"

#include "kwin_export.h"

#include <QObject>

#include <X11/Xlib-xcb.h>
#include <fixx11h.h>
#include <memory>

namespace KWin
{

namespace base::x11
{
class event_filter;
}

namespace render::backend::x11
{
class glx_backend;
class output;

class KWIN_EXPORT platform : public render::platform
{
    Q_OBJECT
public:
    platform(base::x11::platform& base);
    ~platform() override;

    void init();

    gl::backend* get_opengl_backend(render::compositor& compositor) override;
    void render_stop(bool on_shutdown) override;

    bool requiresCompositing() const override;
    bool compositingPossible() const override;
    QString compositingNotPossibleReason() const override;
    bool openGLCompositingIsBroken() const override;
    void createOpenGLSafePoint(OpenGLSafePoint safePoint) override;

    outline_visual* create_non_composited_outline(render::outline* outline) override;
    win::deco::renderer* createDecorationRenderer(win::deco::client_impl* client) override;

    void invertScreen() override;

    void createEffectsHandler(render::compositor* compositor, render::scene* scene) override;
    CompositingType selected_compositor() const override;

private:
    QThread* m_openGLFreezeProtectionThread = nullptr;
    QTimer* m_openGLFreezeProtection = nullptr;

    Display* m_x11Display;
    base::x11::platform& base;

    std::unique_ptr<glx_backend> gl_backend;
};

}
}
