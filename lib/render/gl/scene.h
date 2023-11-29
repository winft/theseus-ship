/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "backend.h"
#include "buffer.h"
#include "deco_renderer.h"
#include "lanczos_filter.h"
#include "window.h"

#include "base/logging.h"
#include "base/options.h"
#include "render/cursor.h"
#include "render/scene.h"
#include "render/shadow.h"

#include <render/gl/interface/platform.h>
#include <render/gl/interface/utils.h>

#include <KNotification>
#include <memory>
#include <unistd.h>
#include <unordered_map>

namespace KWin::render::gl
{

template<typename Platform>
class scene : public render::scene<Platform>
{
public:
    using type = scene<Platform>;
    using abstract_type = render::scene<Platform>;

    using window_t = typename abstract_type::window_t;
    using gl_window_t = gl::window<typename window_t::ref_t, type>;

    // TODO(romangg): Why can't we use the one from the parent class?
    using effect_window_t = typename abstract_type::effect_window_t;

    using backend_t = gl::backend<type, Platform>;
    using buffer_t = buffer<window_t, type>;
    using texture_t = gl::texture<backend_t>;

    using output_t = typename abstract_type::output_t;

    explicit scene(Platform& platform)
        : abstract_type(platform)
        , m_backend{platform.get_opengl_backend()}
    {
        if (!viewportLimitsMatched(platform.base.topology.size)) {
            // TODO(romangg): throw?
            return;
        }

        auto glPlatform = GLPlatform::instance();

        // set strict binding
        if (platform.options->qobject->isGlStrictBindingFollowsDriver()) {
            platform.options->qobject->setGlStrictBinding(!glPlatform->supports(LooseBinding));
        }

        if constexpr (requires(Platform platform) { platform.create_sync(); }) {
            platform.create_sync();
        }

        // We only support the OpenGL 2+ shader API, not GL_ARB_shader_objects
        if (!hasGLVersion(2, 0)) {
            throw std::runtime_error("OpenGL 2.0 is not supported");
        }

        // It is not legal to not have a vertex array object bound in a core context
        if (!GLPlatform::instance()->isGLES()
            && hasGLExtension(QByteArrayLiteral("GL_ARB_vertex_array_object"))) {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
        }

        qCDebug(KWIN_CORE) << "OpenGL 2 compositing successfully initialized";
    }

    ~scene() override
    {
        makeOpenGLContextCurrent();

        // Need to reset early, otherwise the GL context is gone.
        sw_cursor.texture.reset();

        if (lanczos) {
            delete lanczos;
            lanczos = nullptr;
        }

        if constexpr (requires(Platform platform) { platform.sync; }) {
            this->platform.sync = {};
        }
    }

    int64_t paint_output(output_t* output,
                         QRegion damage,
                         std::deque<typename window_t::ref_t> const& ref_wins,
                         std::chrono::milliseconds presentTime) override
    {
        this->createStackingOrder(get_leads(ref_wins));

        m_backend->startRenderTimer();

        // Makes context current on the output.
        auto render = m_backend->set_render_target_to_output(*output);
        auto const repaint = m_backend->get_output_render_region(*output);

        GLVertexBuffer::streamingBuffer()->beginFrame();

        GLenum const status = glGetGraphicsResetStatus();
        if (status != GL_NO_ERROR) {
            handleGraphicsReset(status);
            return 0;
        }

        auto mask = paint_type::none;
        QRegion update;
        QRegion valid;
        this->repaint_output = output;
        vp_projection = render.projection * render.view;

        // Call generic implementation.
        this->paintScreen(render, mask, damage, repaint, &update, &valid, presentTime);
        paintCursor(render);

        assert(render.targets.size() == 1);

        GLVertexBuffer::streamingBuffer()->endOfFrame();
        m_backend->endRenderingFrameForScreen(output, valid, update);

        this->clearStackingOrder();
        this->repaint_output = nullptr;

        return m_backend->renderTime();
    }

    void end_paint() override
    {
        m_backend->try_present();

        if constexpr (requires(Platform platform) { platform.sync; }) {
            if (this->platform.sync && !this->platform.sync->updateFences()) {
                this->platform.sync.reset();
            }
        }
    }

    std::unique_ptr<render::shadow<window_t>> createShadow(window_t* win) override
    {
        return std::make_unique<shadow<window_t, type>>(win, *this);
    }

    void handle_screen_geometry_change(QSize const& size) override
    {
        if (!viewportLimitsMatched(size)) {
            return;
        }
        m_backend->screenGeometryChanged(size);
    }

    bool isOpenGl() const override
    {
        return true;
    }

    bool hasSwapEvent() const override
    {
        return m_backend->hasSwapEvent();
    }

    bool makeOpenGLContextCurrent() override
    {
        return m_backend->makeCurrent();
    }

    void doneOpenGLContextCurrent() override
    {
        m_backend->doneCurrent();
    }

    bool supportsSurfacelessContext() const override
    {
        return m_backend->supportsSurfacelessContext();
    }

    std::unique_ptr<win::deco::render_injector>
    create_deco(win::deco::render_window window) override
    {
        return std::make_unique<deco_renderer<type>>(std::move(window), *this);
    }

    void triggerFence() override
    {
        if constexpr (requires(Platform platform) { platform.sync; }) {
            if (this->platform.sync) {
                this->platform.sync->trigger();
            }
        }
    }

    bool animationsSupported() const override
    {
        return !GLPlatform::instance()->isSoftwareEmulation();
    }

    void insertWait()
    {
        if constexpr (requires(Platform platform) { platform.sync; }) {
            if (this->platform.sync) {
                this->platform.sync->wait();
            }
        }
    }

    void idle() override
    {
        if (m_backend->hasPendingFlush()) {
            makeOpenGLContextCurrent();
            m_backend->try_present();
        }
        render::scene<Platform>::idle();
    }

    /**
     * @brief Factory method to create a backend specific texture.
     *
     * @return scene::texture*
     */
    texture_t* createTexture()
    {
        return new texture_t(m_backend);
    }

    backend_t* backend() const
    {
        return m_backend;
    }

    QVector<QByteArray> openGLPlatformInterfaceExtensions() const override
    {
        return m_backend->extensions().toVector();
    }

    static bool supported(backend_t* backend)
    {
        const QByteArray forceEnv = qgetenv("KWIN_COMPOSE");
        if (!forceEnv.isEmpty()) {
            if (qstrcmp(forceEnv, "O2") == 0 || qstrcmp(forceEnv, "O2ES") == 0) {
                qCDebug(KWIN_CORE) << "OpenGL 2 compositing enforced by environment variable";
                return true;
            } else {
                // OpenGL 2 disabled by environment variable
                return false;
            }
        }
        if (!backend->isDirectRendering()) {
            return false;
        }
        if (GLPlatform::instance()->recommend_sw()) {
            qCDebug(KWIN_CORE) << "Driver does not recommend OpenGL compositing";
            return false;
        }
        return true;
    }

    std::unordered_map<uint32_t, gl_window_t*> windows;

protected:
    std::unique_ptr<window_t> createWindow(typename window_t::ref_t ref_win) override
    {
        return std::make_unique<gl_window_t>(ref_win, *this);
    }

    void finalDrawWindow(effect::window_paint_data& data) override
    {
        auto& eff_win = static_cast<effect_window_t&>(data.window);

        if (base::wayland::is_screen_locked(this->platform.base)) {
            if (std::visit(overload{[&](auto&& win) {
                               if constexpr (requires(decltype(win) win) { win.isLockScreen(); }) {
                                   if (win->isLockScreen()) {
                                       return false;
                                   }
                               }
                               if constexpr (requires(decltype(win) win) { win.isInputMethod(); }) {
                                   if (win->isInputMethod()) {
                                       return false;
                                   }
                               }
                               return true;
                           }},
                           *eff_win.window.ref_win)) {
                return;
            }
        }
        performPaintWindow(data);
    }

    /**
     * Render cursor texture in case hardware cursor is disabled.
     * Useful for screen recording apps or backends that can't do planes.
     */
    void paintCursor(effect::render_data const& render)
    {
        if constexpr (requires(decltype(this->platform) platform) { platform.software_cursor; }) {
            auto cursor = this->platform.software_cursor.get();

            // don't paint if we use hardware cursor or the cursor is hidden
            if (!cursor->enabled || this->platform.base.mod.space->input->cursor->is_hidden()
                || cursor->image().isNull()) {
                return;
            }

            // lazy init texture cursor only in case we need software rendering
            if (sw_cursor.dirty) {
                auto const img = this->platform.software_cursor->image();

                // If there was no new image we are still dirty and try to update again next paint
                // cycle.
                sw_cursor.dirty = img.isNull();

                // With an image we update the texture, or if one was never set before create a
                // default one.
                if (!img.isNull() || !sw_cursor.texture) {
                    sw_cursor.texture.reset(new GLTexture(img));
                }

                // handle shape update on case cursor image changed
                if (!sw_cursor.notifier) {
                    sw_cursor.notifier = QObject::connect(cursor->qobject.get(),
                                                          &render::cursor_qobject::changed,
                                                          this,
                                                          [this] { sw_cursor.dirty = true; });
                }
            }

            // get cursor position in projection coordinates
            auto const cursorPos
                = this->platform.base.mod.space->input->cursor->pos() - cursor->hotspot();
            auto mvp = render.projection * render.view;
            mvp.translate(cursorPos.x(), cursorPos.y());

            // handle transparence
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // paint texture in cursor offset
            sw_cursor.texture->bind();
            ShaderBinder binder(ShaderTrait::MapTexture);
            binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
            sw_cursor.texture->render(sw_cursor.texture->size());
            sw_cursor.texture->unbind();

            cursor->mark_as_rendered();

            glDisable(GL_BLEND);
        }
    }

    void paintBackground(QRegion const& region, QMatrix4x4 const& projection) override
    {
        PaintClipper pc(region);

        if (!PaintClipper::clip()) {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }

        if (pc.clip() && pc.paintArea().isEmpty()) {
            // no background to paint
            return;
        }

        QVector<QVector2D> verts;
        for (PaintClipper::Iterator iterator; !iterator.isDone(); iterator.next()) {
            QRect r = iterator.boundingRect();

            verts.push_back(QVector2D(r.x() + r.width(), r.y()));
            verts.push_back(QVector2D(r.x(), r.y()));
            verts.push_back(QVector2D(r.x(), r.y() + r.height()));
            verts.push_back(QVector2D(r.x(), r.y() + r.height()));
            verts.push_back(QVector2D(r.x() + r.width(), r.y() + r.height()));
            verts.push_back(QVector2D(r.x() + r.width(), r.y()));
        }

        auto vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setVertices(verts);

        ShaderBinder binder(ShaderTrait::UniformColor);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, projection);
        binder.shader()->setUniform(GLShader::ColorUniform::Color, QColor(0, 0, 0, 0));

        vbo->render(GL_TRIANGLES);
    }

    void extendPaintRegion(QRegion& region, bool opaqueFullscreen) override
    {
        if (m_backend->supportsBufferAge())
            return;

        if (this->platform.base.operation_mode == base::operation_mode::x11
            && GLPlatform::instance()->driver() == Driver_NVidia) {
            // Nvidia's X11 driver supports fast full buffer copies. So no need to extend damage.
            // TODO: Do we really need to check this here? Could we just run it anyway or does maybe
            //       not even reach this when we run Nvidia?
            return;
        }

        auto const& screenSize = this->platform.base.topology.size;
        const QRegion displayRegion(0, 0, screenSize.width(), screenSize.height());

        uint damagedPixels = 0;
        const uint fullRepaintLimit
            = (opaqueFullscreen ? 0.49f : 0.748f) * screenSize.width() * screenSize.height();

        // 16:9 is 75% of 4:3 and 2.55:1 is 49.01% of 5:4
        // (5:4 is the most square format and 2.55:1 is Cinemascope55 - the widest ever shot
        // movie aspect - two times ;-) It's a Fox format, though, so maybe we want to restrict
        // to 2.20:1 - Panavision - which has actually been used for interesting movies ...)
        // would be 57% of 5/4
        for (const QRect& r : region) {
            //                 damagedPixels += r.width() * r.height(); // combined window damage
            //                 test
            damagedPixels = r.width() * r.height(); // experimental single window damage testing
            if (damagedPixels > fullRepaintLimit) {
                region = displayRegion;
                return;
            }
        }
    }

    void paintOffscreenQuickView(OffscreenQuickView* view) override
    {
        auto texture = view->bufferAsTexture();
        if (!texture) {
            return;
        }

        ShaderTraits traits = ShaderTrait::MapTexture;
        auto opacity = view->opacity();
        if (opacity != 1.0) {
            traits |= ShaderTrait::Modulate;
        }

        auto shader = ShaderManager::instance()->pushShader(traits);
        auto const rect = view->geometry();

        auto mvp = vp_projection;
        mvp.translate(rect.x(), rect.y());
        shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);

        if (opacity != 1.0) {
            shader->setUniform(GLShader::ModulationConstant,
                               QVector4D(opacity, opacity, opacity, opacity));
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        texture->bind();
        texture->render(rect.size());
        texture->unbind();
        glDisable(GL_BLEND);

        ShaderManager::instance()->popShader();
    }

    void handleGraphicsReset(GLenum status)
    {
        switch (status) {
        case GL_GUILTY_CONTEXT_RESET:
            qCDebug(KWIN_CORE)
                << "A graphics reset attributable to the current GL context occurred.";
            break;

        case GL_INNOCENT_CONTEXT_RESET:
            qCDebug(KWIN_CORE)
                << "A graphics reset not attributable to the current GL context occurred.";
            break;

        case GL_UNKNOWN_CONTEXT_RESET:
            qCDebug(KWIN_CORE) << "A graphics reset of an unknown cause occurred.";
            break;

        default:
            break;
        }

        QElapsedTimer timer;
        timer.start();

        // Wait until the reset is completed or max 10 seconds
        while (timer.elapsed() < 10000 && glGetGraphicsResetStatus() != GL_NO_ERROR)
            usleep(50);

        qCDebug(KWIN_CORE) << "Attempting to reset compositing.";
        QMetaObject::invokeMethod(
            this, [this] { this->platform.reinitialize(); }, Qt::QueuedConnection);

        KNotification::event(QStringLiteral("graphicsreset"),
                             i18n("Desktop effects were restarted due to a graphics reset"));
    }

private:
    bool viewportLimitsMatched(const QSize& size) const
    {
        if (!this->windowing_integration.handle_viewport_limits_alarm) {
            // If we have now way of reacting to the alarm this check is useless.
            return true;
        }

        GLint limit[2];
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, limit);
        if (limit[0] < size.width() || limit[1] < size.height()) {
            this->windowing_integration.handle_viewport_limits_alarm();
            return false;
        }

        return true;
    }

    std::deque<typename window_t::ref_t>
    get_leads(std::deque<typename window_t::ref_t> const& ref_wins)
    {
        std::deque<typename window_t::ref_t> leads;

        for (auto const& ref_win : ref_wins) {
            std::visit(overload{[&](auto&& ref_win) {
                           if (!ref_win->transient->lead() || !ref_win->transient->annexed) {
                               leads.push_back(ref_win);
                               return;
                           }

                           auto const damage = ref_win->render_data.damage_region;
                           if (damage.isEmpty()) {
                               return;
                           }

                           auto lead = win::lead_of_annexed_transient(ref_win);
                           auto const lead_render_geo = win::render_geometry(lead);
                           auto const lead_damage = damage.translated(
                               win::render_geometry(ref_win).topLeft() - lead_render_geo.topLeft());

                           lead->render_data.repaints_region += lead_damage.translated(
                               lead_render_geo.topLeft() - lead->geo.frame.topLeft());
                           lead->render_data.damage_region += lead_damage;

                           for (auto const& rect : lead_damage) {
                               // Emit for thumbnail repaint.
                               Q_EMIT lead->qobject->damaged(rect);
                           }
                       }},
                       ref_win);
        }

        return leads;
    }

    void performPaintWindow(effect::window_paint_data& data)
    {
        auto& eff_win = static_cast<effect_window_t&>(data.window);
        auto mask = static_cast<paint_type>(data.paint.mask);

        if (flags(mask & paint_type::window_lanczos)) {
            if (!lanczos) {
                lanczos = new lanczos_filter<scene>(this);
            }
            lanczos->performPaint(eff_win, mask, data);
        } else
            eff_win.window.performPaint(mask, data);
    }

    backend_t* m_backend;

    lanczos_filter<type>* lanczos{nullptr};

    struct {
        std::unique_ptr<GLTexture> texture;
        bool dirty{true};
        QMetaObject::Connection notifier;
    } sw_cursor;

    QMatrix4x4 vp_projection;
    GLuint vao{0};
};

template<typename Platform>
std::unique_ptr<render::scene<Platform>> create_scene(Platform& platform)
{
    qCDebug(KWIN_CORE) << "Creating OpenGL scene.";

    // Some broken drivers crash on glXQuery() so to prevent constant KWin crashes:
    if (platform.openGLCompositingIsBroken()) {
        throw std::runtime_error("OpenGL library is unsafe to use");
    }

    platform.createOpenGLSafePoint(opengl_safe_point::pre_init);
    auto post = [&] { platform.createOpenGLSafePoint(opengl_safe_point::post_init); };

    try {
        auto scene = std::make_unique<gl::scene<Platform>>(platform);
        post();
        return scene;
    } catch (std::runtime_error const& exc) {
        post();
        throw exc;
    }
}

}
