/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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

#include "backend.h"

#include "render/scene.h"
#include "render/shadow.h"

#include "decorations/decorationrenderer.h"

namespace KWin::render::qpainter
{

class KWIN_EXPORT scene : public render::scene
{
    Q_OBJECT

public:
    explicit scene(qpainter::backend* backend);
    ~scene() override;

    int64_t paint_output(base::output* output,
                         QRegion damage,
                         std::deque<Toplevel*> const& windows,
                         std::chrono::milliseconds presentTime) override;

    void paintGenericScreen(paint_type mask, ScreenPaintData data) override;

    CompositingType compositingType() const override;
    bool initFailed() const override;
    render::effect_frame* createEffectFrame(effect_frame_impl* frame) override;
    render::shadow* createShadow(Toplevel* toplevel) override;
    Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl* impl) override;
    void handle_screen_geometry_change(QSize const& size) override;

    bool animationsSupported() const override
    {
        return false;
    }

    QPainter* scenePainter() const override;
    QImage* qpainterRenderBuffer() const override;

    qpainter::backend* backend() const
    {
        return m_backend.data();
    }

protected:
    void paintBackground(QRegion region) override;
    render::window* createWindow(Toplevel* toplevel) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    QScopedPointer<qpainter::backend> m_backend;
    QScopedPointer<QPainter> m_painter;
};

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
    const QImage& image();

private:
    QImage m_image;
};

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

class shadow : public render::shadow
{
public:
    shadow(Toplevel* toplevel);
    ~shadow() override;

    QImage& shadowTexture()
    {
        return m_texture;
    }

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    QImage m_texture;
};

class deco_renderer : public Decoration::Renderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int { Left, Top, Right, Bottom, Count };
    explicit deco_renderer(Decoration::DecoratedClientImpl* client);
    ~deco_renderer() override;

    void render() override;
    void reparent(Toplevel* window) override;

    QImage image(DecorationPart part) const;

private:
    void resizeImages();
    QImage m_images[int(DecorationPart::Count)];
};

KWIN_EXPORT render::scene* create_scene();

inline QPainter* scene::scenePainter() const
{
    return m_painter.data();
}

inline const QImage& window_pixmap::image()
{
    return m_image;
}

}
