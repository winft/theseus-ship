/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/window.h"

namespace KWin::render::qpainter
{
class scene;

class window : public render::window
{
public:
    window(Toplevel* c, qpainter::scene& scene);
    ~window() override;

    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override;

protected:
    render::buffer* create_buffer() override;

private:
    void renderShadow(QPainter* painter);
    void renderWindowDecorations(QPainter* painter);
};

}
