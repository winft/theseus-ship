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

#include "scene.h"
#include "shadow.h"
#include <platformsupport/scenes/qpainter/backend.h>

#include "decorations/decorationrenderer.h"

namespace KWin::render::qpainter
{

class KWIN_EXPORT scene : public render::scene
{
    Q_OBJECT

public:
    ~scene() override;
    bool usesOverlayWindow() const override;
    OverlayWindow* overlayWindow() const override;

    int64_t paint(base::output* output,
                  QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;

    void paintGenericScreen(int mask, ScreenPaintData data) override;

    CompositingType compositingType() const override;
    bool initFailed() const override;
    render::effect_frame* createEffectFrame(EffectFrameImpl* frame) override;
    Shadow* createShadow(Toplevel* toplevel) override;
    Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl* impl) override;
    void screenGeometryChanged(const QSize& size) override;

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

    static scene* createScene(QObject* parent);

protected:
    void paintBackground(QRegion region) override;
    render::window* createWindow(Toplevel* toplevel) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    explicit scene(qpainter::backend* backend, QObject* parent = nullptr);
    QScopedPointer<qpainter::backend> m_backend;
    QScopedPointer<QPainter> m_painter;
};

class window : public render::window
{
public:
    window(qpainter::scene* scene, Toplevel* c);
    ~window() override;
    void performPaint(int mask, QRegion region, WindowPaintData data) override;

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
    effect_frame(EffectFrameImpl* frame, qpainter::scene* scene);
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

class shadow : public KWin::Shadow
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

class KWIN_EXPORT scene_factory : public render::scene_factory
{
    Q_OBJECT
    Q_INTERFACES(KWin::render::scene_factory)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Scene" FILE "qpainter.json")

public:
    explicit scene_factory(QObject* parent = nullptr);
    ~scene_factory() override;

    render::scene* create(QObject* parent = nullptr) const override;
};

inline bool scene::usesOverlayWindow() const
{
    return false;
}

inline OverlayWindow* scene::overlayWindow() const
{
    return nullptr;
}

inline QPainter* scene::scenePainter() const
{
    return m_painter.data();
}

inline const QImage& window_pixmap::image()
{
    return m_image;
}

}
