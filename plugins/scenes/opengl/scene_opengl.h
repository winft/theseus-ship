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

#include "scene.h"
#include "shadow.h"

#include "kwinglutils.h"

#include "decorations/decorationrenderer.h"
#include "platformsupport/scenes/opengl/backend.h"

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
    class EffectFrame;
    ~scene() override;
    bool initFailed() const override;
    bool hasPendingFlush() const override;

    qint64 paint(QRegion damage,
                 std::deque<Toplevel*> const& windows,
                 std::chrono::milliseconds presentTime) override;
    int64_t paint(base::output* output,
                  QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;

    render::scene::EffectFrame* createEffectFrame(EffectFrameImpl* frame) override;
    Shadow* createShadow(Toplevel* toplevel) override;
    void screenGeometryChanged(const QSize& size) override;
    OverlayWindow* overlayWindow() const override;
    bool usesOverlayWindow() const override;
    bool hasSwapEvent() const override;
    bool makeOpenGLContextCurrent() override;
    void doneOpenGLContextCurrent() override;
    bool supportsSurfacelessContext() const override;
    Decoration::Renderer* createDecorationRenderer(Decoration::DecoratedClientImpl* impl) override;
    void triggerFence() override;
    virtual QMatrix4x4 projectionMatrix() const = 0;
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

    static scene* createScene(QObject* parent);

    std::unordered_map<uint32_t, window*> windows;

protected:
    scene(render::gl::backend* backend, QObject* parent = nullptr);
    void paintBackground(QRegion region) override;
    void extendPaintRegion(QRegion& region, bool opaqueFullscreen) override;
    QMatrix4x4 transformation(int mask, const ScreenPaintData& data) const;
    void paintDesktop(int desktop, int mask, const QRegion& region, ScreenPaintData& data) override;
    void paintEffectQuickView(EffectQuickView* w) override;

    void handleGraphicsReset(GLenum status);

    virtual void doPaintBackground(const QVector<float>& vertices) = 0;
    virtual void updateProjectionMatrix() = 0;

    bool init_ok;

private:
    bool viewportLimitsMatched(const QSize& size) const;
    std::deque<Toplevel*> get_leads(std::deque<Toplevel*> const& windows);

    render::gl::backend* m_backend;
    SyncManager* m_syncManager;
    SyncObject* m_currentFence;
    bool m_debug;
};

class scene2 : public scene
{
    Q_OBJECT
public:
    explicit scene2(render::gl::backend* backend, QObject* parent = nullptr);
    ~scene2() override;
    CompositingType compositingType() const override
    {
        return OpenGLCompositing;
    }

    static bool supported(render::gl::backend* backend);

    QMatrix4x4 projectionMatrix() const override
    {
        return m_projectionMatrix;
    }
    QMatrix4x4 screenProjectionMatrix() const override
    {
        return m_screenProjectionMatrix;
    }

protected:
    void paintSimpleScreen(int mask, QRegion region) override;
    void paintGenericScreen(int mask, ScreenPaintData data) override;
    void doPaintBackground(const QVector<float>& vertices) override;
    render::window* createWindow(Toplevel* t) override;
    void
    finalDrawWindow(EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data) override;
    void updateProjectionMatrix() override;
    void paintCursor() override;

private:
    void performPaintWindow(EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data);
    QMatrix4x4 createProjectionMatrix() const;

private:
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
    void performPaint(int mask, QRegion region, WindowPaintData data) override;

private:
    QMatrix4x4 transformation(int mask, const WindowPaintData& data) const;
    GLTexture* getDecorationTexture() const;
    QMatrix4x4 modelViewProjectionMatrix(int mask, const WindowPaintData& data) const;
    QVector4D modulate(float opacity, float brightness) const;
    void setBlendEnabled(bool enabled);
    void setupLeafNodes(std::vector<LeafNode>& nodes,
                        std::vector<WindowQuadList> const& quads,
                        bool has_previous_content,
                        WindowPaintData const& data);
    bool beginRenderWindow(int mask, const QRegion& region, WindowPaintData& data);
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

class scene::EffectFrame : public render::scene::EffectFrame
{
public:
    EffectFrame(EffectFrameImpl* frame, gl::scene* scene);
    ~EffectFrame() override;

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
class shadow : public KWin::Shadow
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

inline bool scene::usesOverlayWindow() const
{
    return m_backend->usesOverlayWindow();
}

inline render::gl::texture* window_pixmap::texture() const
{
    return m_texture.data();
}

class KWIN_EXPORT scene_factory : public render::scene_factory
{
    Q_OBJECT
    Q_INTERFACES(KWin::render::scene_factory)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Scene" FILE "opengl.json")

public:
    explicit scene_factory(QObject* parent = nullptr);
    ~scene_factory() override;

    render::scene* create(QObject* parent = nullptr) const override;
};

}
}
