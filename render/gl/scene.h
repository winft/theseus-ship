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

#include "render/scene.h"
#include "render/shadow.h"

#include <kwingl/utils.h>

#include <memory>
#include <unordered_map>

namespace KWin::render
{

class compositor;

namespace gl
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
    explicit scene(render::compositor& compositor);
    ~scene() override;

    int64_t paint(QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;
    int64_t paint_output(base::output* output,
                         QRegion damage,
                         std::deque<Toplevel*> const& windows,
                         std::chrono::milliseconds presentTime) override;

    std::unique_ptr<render::shadow> createShadow(Toplevel* toplevel) override;
    void handle_screen_geometry_change(QSize const& size) override;
    CompositingType compositingType() const override;
    bool hasSwapEvent() const override;
    bool makeOpenGLContextCurrent() override;
    void doneOpenGLContextCurrent() override;
    bool supportsSurfacelessContext() const override;
    win::deco::renderer<win::deco::client_impl>*
    createDecorationRenderer(win::deco::client_impl* impl) override;
    void triggerFence() override;
    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 screenProjectionMatrix() const override;
    bool animationsSupported() const override;

    void insertWait();

    void idle() override;

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
    std::unique_ptr<render::window> createWindow(Toplevel* t) override;
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

    lanczos_filter* lanczos{nullptr};

    struct {
        std::unique_ptr<GLTexture> texture;
        bool dirty{true};
        QMetaObject::Connection notifier;
    } sw_cursor;

    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_screenProjectionMatrix;
    GLuint vao{0};
};

KWIN_EXPORT std::unique_ptr<render::scene> create_scene(render::compositor& compositor);

}
}
