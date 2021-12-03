/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QRegion>

namespace KWin::render
{

class effect_frame_impl;

class effect_frame
{
public:
    effect_frame(effect_frame_impl* frame)
        : m_effectFrame(frame)
    {
    }
    virtual ~effect_frame() = default;

    virtual void render(QRegion region, double opacity, double frameOpacity) = 0;

    virtual void free() = 0;
    virtual void freeIconFrame() = 0;
    virtual void freeTextFrame() = 0;
    virtual void freeSelection() = 0;

    virtual void crossFadeIcon() = 0;
    virtual void crossFadeText() = 0;

protected:
    effect_frame_impl* m_effectFrame;
};

}
