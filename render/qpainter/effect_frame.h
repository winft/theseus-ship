/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect_frame.h"

namespace KWin::render::qpainter
{

class scene;

class effect_frame : public render::effect_frame
{
public:
    effect_frame(effect_frame_impl* frame, qpainter::scene* scene);
    ~effect_frame() override;

    void crossFadeIcon() override
    {
    }
    void crossFadeText() override
    {
    }

    void free() override
    {
    }
    void freeIconFrame() override
    {
    }
    void freeTextFrame() override
    {
    }
    void freeSelection() override
    {
    }

    void render(QRegion region, double opacity, double frameOpacity) override;

private:
    scene* m_scene;
};

}
