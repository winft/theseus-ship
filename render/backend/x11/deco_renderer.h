/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco/renderer.h"

#include <xcb/xcb.h>

class QTimer;

namespace KWin::render::backend::x11
{

class deco_renderer : public win::deco::renderer
{
    Q_OBJECT
public:
    explicit deco_renderer(win::deco::client_impl* client);
    ~deco_renderer() override;

    void reparent(Toplevel* window) override;

protected:
    void render() override;

private:
    QTimer* m_scheduleTimer;
    xcb_gcontext_t m_gc;
};

}
