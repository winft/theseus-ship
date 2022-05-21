/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco/renderer.h"

#include <kwinxrender/utils.h>

namespace KWin::render::xrender
{

class deco_renderer : public win::deco::renderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int { Left, Top, Right, Bottom, Count };
    explicit deco_renderer(win::deco::client_impl* client);
    ~deco_renderer() override;

    void render() override;
    void reparent() override;

    xcb_render_picture_t picture(DecorationPart part) const;

private:
    void resizePixmaps();
    QSize m_sizes[int(DecorationPart::Count)];
    xcb_pixmap_t m_pixmaps[int(DecorationPart::Count)];
    xcb_gcontext_t m_gc;
    XRenderPicture* m_pictures[int(DecorationPart::Count)];
};

}
