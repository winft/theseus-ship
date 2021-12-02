/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

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

#include "decorations/decorationrenderer.h"
#include "kwinglutils.h"
#include "render/scene.h"
#include "render/shadow.h"

#include <unordered_map>

namespace KWin
{

namespace render::gl
{
class backend;
class lanczos_filter;
class SyncManager;
class SyncObject;
class window;

class KWIN_EXPORT scene : public render::scene
{
    Q_OBJECT
public:
    explicit scene(render::gl::backend* backend);
    ~scene() override;
    bool initFailed() const override;
    bool hasPendingFlush() const override;

    int64_t paint(QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;
    int64_t paint_output(base::output* output,
                         QRegion damage,
                         std::deque<Toplevel*> const& windows,
                         std::chrono::milliseconds presentTime) override;

    render::effect_frame* createEffectFrame(effect_frame_impl* frame) override;
    render::shadow* createShadow(Toplevel* toplevel) override;
    void handle_screen_geometry_change(QSize const& size) override;
    CompositingType compositingType() const override;
    bool hasSwapEvent() const override;
    bool makeOpenGLContextCurrent() override;
    void doneOpenGLContextCurrent() override;
    bool supportsSurfacelessContext() const override;
    Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl* impl) override;
    void triggerFence() override;
    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 screenProjectionMatrix() const override;
    bool animationsSupported() const override;

    void insertWait();

    void idle() override;

    bool debug() const
    {
        return m_debug;
    }
    void initDebugOutput();

    /**
     * @brief Factory method to create a backend specific texture.
     *
     * @return scene::texture*
     */
    render::gl::texture* createTexture();

    render::gl::backend* backend() const
    {
        return m_backend;
    }

    QVector<QByteArray> openGLPlatformInterfaceExtensions() const override;

    static bool supported(render::gl::backend* backend);

    std::unordered_map<uint32_t, window*> windows;

protected:
    void paintSimpleScreen(paint_type mask, QRegion region) override;
    void paintGenericScreen(paint_type mask, ScreenPaintData data) override;
    render::window* createWindow(Toplevel* t) override;
    void finalDrawWindow(effects_window_impl* w,
                         paint_type mask,
                         QRegion region,
                         WindowPaintData& data) override;
    void paintCursor() override;

    void paintBackground(QRegion region) override;
    void extendPaintRegion(QRegion& region, bool opaqueFullscreen) override;
    QMatrix4x4 transformation(paint_type mask, const ScreenPaintData& data) const;
    void paintDesktop(int desktop,
                      paint_type mask,
                      const QRegion& region,
                      ScreenPaintData& data) override;
    void paintEffectQuickView(EffectQuickView* w) override;

    void handleGraphicsReset(GLenum status);

    void doPaintBackground(const QVector<float>& vertices);
    void updateProjectionMatrix();

    bool init_ok{true};

private:
    bool viewportLimitsMatched(const QSize& size) const;
    std::deque<Toplevel*> get_leads(std::deque<Toplevel*> const& windows);

    void performPaintWindow(effects_window_impl* w,
                            paint_type mask,
                            QRegion region,
                            WindowPaintData& data);
    QMatrix4x4 createProjectionMatrix() const;

    render::gl::backend* m_backend;
    SyncManager* m_syncManager{nullptr};
    SyncObject* m_currentFence{nullptr};
    bool m_debug{false};

    lanczos_filter* lanczos{nullptr};
    QScopedPointer<GLTexture> m_cursorTexture;
    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_screenProjectionMatrix;
    GLuint vao;
};

class window_pixmap;

class window final : public render::window
{
public:
    enum Leaf { ShadowLeaf = 0, DecorationLeaf, ContentLeaf, PreviousContentLeaf, LeafCount };

    struct LeafNode {
        LeafNode()
            : texture(nullptr)
            , firstVertex(0)
            , vertexCount(0)
            , opacity(1.0)
            , hasAlpha(false)
            , coordinateType(UnnormalizedCoordinates)
        {
        }

        GLTexture* texture;
        int firstVertex;
        int vertexCount;
        float opacity;
        bool hasAlpha;
        TextureCoordinateType coordinateType;
    };

    window(Toplevel* toplevel, gl::scene* scene);
    ~window() override;

    render::window_pixmap* createWindowPixmap() override;
    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override;

private:
    QMatrix4x4 transformation(paint_type mask, const WindowPaintData& data) const;
    GLTexture* getDecorationTexture() const;
    QMatrix4x4 modelViewProjectionMatrix(paint_type mask, const WindowPaintData& data) const;
    QVector4D modulate(float opacity, float brightness) const;
    void setBlendEnabled(bool enabled);
    void setupLeafNodes(std::vector<LeafNode>& nodes,
                        std::vector<WindowQuadList> const& quads,
                        bool has_previous_content,
                        WindowPaintData const& data);
    bool beginRenderWindow(paint_type mask, const QRegion& region, WindowPaintData& data);
    void endRenderWindow();
    render::gl::texture* bindTexture();

    scene* m_scene;
    bool m_hardwareClipping = false;
    bool m_blendingEnabled = false;
};

class window_pixmap : public render::window_pixmap
{
public:
    explicit window_pixmap(render::window* window, gl::scene* scene);
    ~window_pixmap() override;
    render::gl::texture* texture() const;
    bool bind();
    bool isValid() const override;

private:
    QScopedPointer<render::gl::texture> m_texture;
    scene* m_scene;
};

class effect_frame : public render::effect_frame
{
public:
    effect_frame(effect_frame_impl* frame, gl::scene* scene);
    ~effect_frame() override;

    void free() override;
    void freeIconFrame() override;
    void freeTextFrame() override;
    void freeSelection() override;

    void render(QRegion region, double opacity, double frameOpacity) override;

    void crossFadeIcon() override;
    void crossFadeText() override;

    static void cleanup();

private:
    void updateTexture();
    void updateTextTexture();

    GLTexture* m_texture;
    GLTexture* m_textTexture;
    GLTexture* m_oldTextTexture;
    QPixmap* m_textPixmap; // need to keep the pixmap around to workaround some driver problems
    GLTexture* m_iconTexture;
    GLTexture* m_oldIconTexture;
    GLTexture* m_selectionTexture;
    GLVertexBuffer* m_unstyledVBO;
    scene* m_scene;

    static GLTexture* m_unstyledTexture;
    static QPixmap*
        m_unstyledPixmap; // need to keep the pixmap around to workaround some driver problems
    static void updateUnstyledTexture(); // Update OpenGL unstyled frame texture
};

/**
 * @short OpenGL implementation of Shadow.
 *
 * This class extends Shadow by the Elements required for OpenGL rendering.
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
class shadow : public render::shadow
{
public:
    explicit shadow(Toplevel* toplevel);
    ~shadow() override;

    GLTexture* shadowTexture()
    {
        return m_texture.data();
    }

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    QSharedPointer<GLTexture> m_texture;
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

    GLTexture* texture()
    {
        return m_texture.data();
    }
    GLTexture* texture() const
    {
        return m_texture.data();
    }

private:
    void resizeTexture();
    QScopedPointer<GLTexture> m_texture;
};

inline bool scene::hasPendingFlush() const
{
    return m_backend->hasPendingFlush();
}

inline render::gl::texture* window_pixmap::texture() const
{
    return m_texture.data();
}

KWIN_EXPORT render::scene* create_scene(render::compositor* compositor);

}
}
