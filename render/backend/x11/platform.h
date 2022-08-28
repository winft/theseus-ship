/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/x11/platform.h"

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
template<typename Compositor>
class glx_backend;
class output;

class KWIN_EXPORT platform : public render::x11::platform
{
public:
    platform(base::x11::platform& base);
    ~platform() override;

    void init();

    gl::backend* get_opengl_backend(render::compositor& compositor) override;
    void render_stop(bool on_shutdown) override;

    bool compositingPossible() const override;
    QString compositingNotPossibleReason() const override;
    void createOpenGLSafePoint(OpenGLSafePoint safePoint) override;

    outline_visual* create_non_composited_outline(render::outline* outline) override;
    win::deco::renderer<win::deco::client_impl<Toplevel>>*
    createDecorationRenderer(win::deco::client_impl<Toplevel>* client) override;

    void invertScreen() override;

    CompositingType selected_compositor() const override;

    base::x11::platform& base;

private:
    std::unique_ptr<QThread> m_openGLFreezeProtectionThread;
    QTimer* m_openGLFreezeProtection = nullptr;
    Display* m_x11Display;

    std::unique_ptr<glx_backend<platform>> gl_backend;
};

}
}
