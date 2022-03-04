/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect_frame.h"

#include <kwinxrender/utils.h>

namespace KWin::render::xrender
{

class effect_frame : public render::effect_frame
{
public:
    effect_frame(effect_frame_impl* frame);
    ~effect_frame() override;

    void free() override;
    void freeIconFrame() override;
    void freeTextFrame() override;
    void freeSelection() override;
    void crossFadeIcon() override;
    void crossFadeText() override;

    void render(QRegion region, double opacity, double frameOpacity) override;

    static void cleanup();

private:
    void updatePicture();
    void updateTextPicture();
    void renderUnstyled(xcb_render_picture_t pict, const QRect& rect, qreal opacity);

    XRenderPicture* m_picture;
    XRenderPicture* m_textPicture;
    XRenderPicture* m_iconPicture;
    XRenderPicture* m_selectionPicture;

    static XRenderPicture* s_effectFrameCircle;
};

}
