/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_DECORATION_X11_RENDERER_H
#define KWIN_DECORATION_X11_RENDERER_H

#include "decorations/decorationrenderer.h"

#include <xcb/xcb.h>

class QTimer;

namespace KWin::render::backend::x11
{

class X11DecoRenderer : public Decoration::Renderer
{
    Q_OBJECT
public:
    explicit X11DecoRenderer(Decoration::DecoratedClientImpl* client);
    ~X11DecoRenderer() override;

    void reparent(Toplevel* window) override;

protected:
    void render() override;

private:
    QTimer* m_scheduleTimer;
    xcb_gcontext_t m_gc;
};

}

#endif
