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

#include "render/scene.h"

namespace KWin::render
{

namespace x11
{
class compositor;
}

namespace xrender
{

class backend;

class scene : public render::scene
{
    Q_OBJECT
public:
    scene(xrender::backend* backend, render::compositor& compositor);
    ~scene() override;

    CompositingType compositingType() const override
    {
        return XRenderCompositing;
    }
    int64_t paint(QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;
    render::effect_frame* createEffectFrame(effect_frame_impl* frame) override;
    std::unique_ptr<render::shadow> createShadow(Toplevel* toplevel) override;
    void handle_screen_geometry_change(QSize const& size) override;
    xcb_render_picture_t xrenderBufferPicture() const override;
    win::deco::renderer* createDecorationRenderer(win::deco::client_impl* client) override;

    bool animationsSupported() const override
    {
        return true;
    }

    static ScreenPaintData screen_paint;

protected:
    std::unique_ptr<render::window> createWindow(Toplevel* toplevel) override;
    void paintBackground(QRegion region) override;
    void paintGenericScreen(paint_type mask, ScreenPaintData data) override;
    void paintDesktop(int desktop,
                      paint_type mask,
                      const QRegion& region,
                      ScreenPaintData& data) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    QScopedPointer<xrender::backend> m_backend;
};

KWIN_EXPORT std::unique_ptr<render::scene> create_scene(x11::compositor& compositor);

}
}
