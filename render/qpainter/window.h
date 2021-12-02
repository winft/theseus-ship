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
    window(qpainter::scene* scene, Toplevel* c);
    ~window() override;

    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override;

protected:
    render::window_pixmap* createWindowPixmap() override;

private:
    void renderShadow(QPainter* painter);
    void renderWindowDecorations(QPainter* painter);

    scene* m_scene;
};

class window_pixmap : public render::window_pixmap
{
public:
    explicit window_pixmap(render::window* window);
    ~window_pixmap() override;

    void create() override;
    bool isValid() const override;

    void updateBuffer() override;
    QImage const& image();

private:
    QImage m_image;
};

}
