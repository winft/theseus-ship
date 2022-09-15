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
#include "buffer.h"
#include "deco_renderer.h"
#include "lanczos_filter.h"
#include "window.h"

#include "base/logging.h"
#include "base/options.h"
#include "render/cursor.h"
#include "render/scene.h"
#include "render/shadow.h"

#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KNotification>
#include <memory>
#include <unistd.h>
#include <unordered_map>
#include <xcb/sync.h>

namespace KWin::render::gl
{

/**
 * SyncObject represents a fence used to synchronize operations in
 * the kwin command stream with operations in the X command stream.
 */
class SyncObject
{
public:
    enum State {
        Ready,
        TriggerSent,
        Waiting,
        Done,
        Resetting,
    };

    SyncObject()
    {
        m_state = Ready;

        xcb_connection_t* const c = connection();

        m_fence = xcb_generate_id(c);
        xcb_sync_create_fence(c, rootWindow(), m_fence, false);
        xcb_flush(c);

        m_sync = glImportSyncEXT(GL_SYNC_X11_FENCE_EXT, m_fence, 0);
        m_reset_cookie.sequence = 0;
    }

    ~SyncObject()
    {
        // If glDeleteSync is called before the xcb fence is signalled
        // the nvidia driver (the only one to implement GL_SYNC_X11_FENCE_EXT)
        // deadlocks waiting for the fence to be signalled.
        // To avoid this, make sure the fence is signalled before
        // deleting the sync.
        if (m_state == Resetting || m_state == Ready) {
            trigger();
            // The flush is necessary!
            // The trigger command needs to be sent to the X server.
            xcb_flush(connection());
        }
        xcb_sync_destroy_fence(connection(), m_fence);
        glDeleteSync(m_sync);

        if (m_state == Resetting)
            xcb_discard_reply(connection(), m_reset_cookie.sequence);
    }

    State state() const
    {
        return m_state;
    }

    void trigger()
    {
        Q_ASSERT(m_state == Ready || m_state == Resetting);

        // Finish resetting the fence if necessary
        if (m_state == Resetting)
            finishResetting();

        xcb_sync_trigger_fence(connection(), m_fence);
        m_state = TriggerSent;
    }

    void wait()
    {
        if (m_state != TriggerSent)
            return;

        glWaitSync(m_sync, 0, GL_TIMEOUT_IGNORED);
        m_state = Waiting;
    }

    bool finish()
    {
        if (m_state == Done)
            return true;

        // Note: It is possible that we never inserted a wait for the fence.
        //       This can happen if we ended up not rendering the damaged
        //       window because it is fully occluded.
        Q_ASSERT(m_state == TriggerSent || m_state == Waiting);

        // Check if the fence is signaled
        GLint value;
        glGetSynciv(m_sync, GL_SYNC_STATUS, 1, nullptr, &value);

        if (value != GL_SIGNALED) {
            qCDebug(KWIN_CORE) << "Waiting for X fence to finish";

            // Wait for the fence to become signaled with a one second timeout
            const GLenum result = glClientWaitSync(m_sync, 0, 1000000000);

            switch (result) {
            case GL_TIMEOUT_EXPIRED:
                qCWarning(KWIN_CORE) << "Timeout while waiting for X fence";
                return false;

            case GL_WAIT_FAILED:
                qCWarning(KWIN_CORE) << "glClientWaitSync() failed";
                return false;
            }
        }

        m_state = Done;
        return true;
    }

    void reset()
    {
        Q_ASSERT(m_state == Done);

        xcb_connection_t* const c = connection();

        // Send the reset request along with a sync request.
        // We use the cookie to ensure that the server has processed the reset
        // request before we trigger the fence and call glWaitSync().
        // Otherwise there is a race condition between the reset finishing and
        // the glWaitSync() call.
        xcb_sync_reset_fence(c, m_fence);
        m_reset_cookie = xcb_get_input_focus(c);
        xcb_flush(c);

        m_state = Resetting;
    }

    void finishResetting()
    {
        Q_ASSERT(m_state == Resetting);
        free(xcb_get_input_focus_reply(connection(), m_reset_cookie, nullptr));
        m_state = Ready;
    }

private:
    State m_state;
    GLsync m_sync;
    xcb_sync_fence_t m_fence;
    xcb_get_input_focus_cookie_t m_reset_cookie;
};

/**
 * SyncManager manages a set of fences used for explicit synchronization
 * with the X command stream.
 */
class SyncManager
{
public:
    enum { MaxFences = 4 };

    SyncObject* nextFence()
    {
        SyncObject* fence = &m_fences[m_next];
        m_next = (m_next + 1) % MaxFences;
        return fence;
    }

    bool updateFences()
    {
        for (int i = 0; i < qMin(2, MaxFences - 1); i++) {
            const int index = (m_next + i) % MaxFences;
            SyncObject& fence = m_fences[index];

            switch (fence.state()) {
            case SyncObject::Ready:
                break;

            case SyncObject::TriggerSent:
            case SyncObject::Waiting:
                if (!fence.finish())
                    return false;
                fence.reset();
                break;

            // Should not happen in practice since we always reset the fence
            // after finishing it
            case SyncObject::Done:
                fence.reset();
                break;

            case SyncObject::Resetting:
                fence.finishResetting();
                break;
            }
        }

        return true;
    }

private:
    std::array<SyncObject, MaxFences> m_fences;
    int m_next{0};
};

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
        : render::scene<Platform>(platform)
        , m_backend{platform.get_opengl_backend(*platform.compositor)}
    {
        if (!viewportLimitsMatched(platform.base.topology.size)) {
            // TODO(romangg): throw?
            return;
        }

        auto glPlatform = GLPlatform::instance();

        // set strict binding
        if (kwinApp()->options->qobject->isGlStrictBindingFollowsDriver()) {
            kwinApp()->options->qobject->setGlStrictBinding(!glPlatform->supports(LooseBinding));
        }

        bool haveSyncObjects = glPlatform->isGLES()
            ? hasGLVersion(3, 0)
            : hasGLVersion(3, 2) || hasGLExtension("GL_ARB_sync");

        if (hasGLExtension("GL_EXT_x11_sync_object") && haveSyncObjects
            && kwinApp()->operationMode() == Application::OperationModeX11) {
            const QByteArray useExplicitSync = qgetenv("KWIN_EXPLICIT_SYNC");

            if (useExplicitSync != "0") {
                qCDebug(KWIN_CORE)
                    << "Initializing fences for synchronization with the X command stream";
                m_syncManager = new SyncManager;
            } else {
                qCDebug(KWIN_CORE) << "Explicit synchronization with the X command stream disabled "
                                      "by environment variable";
            }
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

        delete m_syncManager;
    }

    int64_t paint(QRegion damage,
                  std::deque<typename window_t::ref_t*> const& toplevels,
                  std::chrono::milliseconds presentTime) override
    {
        // Remove all subordinate transients. These are painted as part of their leads.
        // TODO: Optimize this by *not* painting them as part of their leads if no quad transforming
        //       (and opacity changing or animated?) effects are active.
        auto const leads = get_leads(toplevels);
        this->createStackingOrder(leads);

        // After this call, update will contain the damaged region in the back buffer. This is the
        // region that needs to be posted to repair the front buffer. It doesn't include the
        // additional damage returned by prepareRenderingFrame(). The valid region is the region
        // that has been repainted, and may be larger than the update region.
        QRegion update;
        QRegion valid;

        m_backend->makeCurrent();
        auto const repaint = m_backend->prepareRenderingFrame();
        GLVertexBuffer::streamingBuffer()->beginFrame();

        GLenum const status = glGetGraphicsResetStatus();
        if (status != GL_NO_ERROR) {
            handleGraphicsReset(status);
            return 0;
        }

        this->m_renderTargetRect = QRect(QPoint(), this->platform.base.topology.size);
        this->m_renderTargetScale = 1.;

        auto mask = paint_type::none;
        updateProjectionMatrix();

        // Call generic implementation.
        this->paintScreen(mask, damage, repaint, &update, &valid, presentTime, projectionMatrix());

        if (!GLPlatform::instance()->isGLES()) {
            auto const displayRegion = QRegion(
                0, 0, this->m_renderTargetRect.width(), this->m_renderTargetRect.height());

            // Copy dirty parts from front to backbuffer.
            if (!m_backend->supportsBufferAge() && GLPlatform::instance()->driver() == Driver_NVidia
                && valid != displayRegion) {
                glReadBuffer(GL_FRONT);
                m_backend->copyPixels(displayRegion - valid);
                glReadBuffer(GL_BACK);
                valid = displayRegion;
            }
        }

        GLVertexBuffer::streamingBuffer()->endOfFrame();
        m_backend->endRenderingFrame(valid, update);

        if (m_currentFence) {
            if (!m_syncManager->updateFences()) {
                qCDebug(KWIN_CORE)
                    << "Aborting explicit synchronization with the X command stream.";
                qCDebug(KWIN_CORE) << "Future frames will be rendered unsynchronized.";
                delete m_syncManager;
                m_syncManager = nullptr;
            }
            m_currentFence = nullptr;
        }

        this->clearStackingOrder();
        return m_backend->renderTime();
    }

    int64_t paint_output(output_t* output,
                         QRegion damage,
                         std::deque<typename window_t::ref_t*> const& windows,
                         std::chrono::milliseconds presentTime) override
    {
        this->createStackingOrder(get_leads(windows));

        // Trigger render timer start.
        m_backend->prepareRenderingFrame();

        // Makes context current on the output.
        auto const repaint = m_backend->prepareRenderingForScreen(output);
        GLVertexBuffer::streamingBuffer()->beginFrame();

        this->m_renderTargetRect = output->geometry();
        this->m_renderTargetScale = output->scale();

        GLenum const status = glGetGraphicsResetStatus();
        if (status != GL_NO_ERROR) {
            handleGraphicsReset(status);
            return 0;
        }

        updateProjectionMatrix();

        auto mask = paint_type::none;
        QRegion update;
        QRegion valid;
        this->repaint_output = output;

        // Call generic implementation.
        this->paintScreen(mask,
                          damage.intersected(this->m_renderTargetRect),
                          repaint,
                          &update,
                          &valid,
                          presentTime,
                          projectionMatrix());
        paintCursor();

        GLVertexBuffer::streamingBuffer()->endOfFrame();
        m_backend->endRenderingFrameForScreen(output, valid, update);

        this->clearStackingOrder();
        this->repaint_output = nullptr;

        return m_backend->renderTime();
    }

    std::unique_ptr<render::shadow<window_t>> createShadow(window_t* window) override
    {
        return std::make_unique<shadow<window_t, type>>(window, *this);
    }

    void handle_screen_geometry_change(QSize const& size) override
    {
        if (!viewportLimitsMatched(size)) {
            return;
        }
        glViewport(0, 0, size.width(), size.height());
        m_backend->screenGeometryChanged(size);
    }

    CompositingType compositingType() const override
    {
        return OpenGLCompositing;
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

    win::deco::renderer<win::deco::client_impl<typename window_t::ref_t>>*
    createDecorationRenderer(win::deco::client_impl<typename window_t::ref_t>* impl) override
    {
        return new deco_renderer(impl, *this);
    }

    void triggerFence() override
    {
        if (m_syncManager) {
            m_currentFence = m_syncManager->nextFence();
            m_currentFence->trigger();
        }
    }

    QMatrix4x4 projectionMatrix() const
    {
        return m_projectionMatrix;
    }

    QMatrix4x4 screenProjectionMatrix() const override
    {
        return m_screenProjectionMatrix;
    }

    bool animationsSupported() const override
    {
        return !GLPlatform::instance()->isSoftwareEmulation();
    }

    void insertWait()
    {
        if (m_currentFence && m_currentFence->state() != SyncObject::Waiting) {
            m_currentFence->wait();
        }
    }

    void idle() override
    {
        if (m_backend->hasPendingFlush()) {
            makeOpenGLContextCurrent();
            m_backend->present();
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
        if (GLPlatform::instance()->recommendedCompositor() != OpenGLCompositing) {
            qCDebug(KWIN_CORE) << "Driver does not recommend OpenGL compositing";
            return false;
        }
        return true;
    }

    std::unordered_map<uint32_t, gl_window_t*> windows;

protected:
    void paintSimpleScreen(paint_type mask, QRegion region) override
    {
        m_screenProjectionMatrix = m_projectionMatrix;

        render::scene<Platform>::paintSimpleScreen(mask, region);
    }

    void paintGenericScreen(paint_type mask, ScreenPaintData data) override
    {
        const QMatrix4x4 screenMatrix = transformation(mask, data);

        m_screenProjectionMatrix = m_projectionMatrix * screenMatrix;

        render::scene<Platform>::paintGenericScreen(mask, data);
    }

    std::unique_ptr<window_t> createWindow(typename window_t::ref_t* t) override
    {
        return std::make_unique<gl_window_t>(t, *this);
    }

    void finalDrawWindow(effect_window_t* w,
                         paint_type mask,
                         QRegion region,
                         WindowPaintData& data) override
    {
        if (kwinApp()->is_screen_locked() && !w->window.ref_win->isLockScreen()
            && !w->window.ref_win->isInputMethod()) {
            return;
        }
        performPaintWindow(w, mask, region, data);
    }

    /**
     * Render cursor texture in case hardware cursor is disabled.
     * Useful for screen recording apps or backends that can't do planes.
     */
    void paintCursor() override
    {
        auto cursor = this->platform.compositor->software_cursor.get();

        // don't paint if we use hardware cursor or the cursor is hidden
        if (!cursor->enabled || this->platform.base.space->input->cursor->is_hidden()
            || cursor->image().isNull()) {
            return;
        }

        // lazy init texture cursor only in case we need software rendering
        if (sw_cursor.dirty) {
            auto const img = this->platform.compositor->software_cursor->image();

            // If there was no new image we are still dirty and try to update again next paint
            // cycle.
            sw_cursor.dirty = img.isNull();

            // With an image we update the texture, or if one was never set before create a default
            // one.
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
        auto const cursorPos = this->platform.base.space->input->cursor->pos() - cursor->hotspot();
        auto const cursorRect
            = QRect(0, 0, sw_cursor.texture->width(), sw_cursor.texture->height());
        QMatrix4x4 mvp = m_projectionMatrix;
        mvp.translate(cursorPos.x(), cursorPos.y());

        // handle transparence
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // paint texture in cursor offset
        sw_cursor.texture->bind();
        ShaderBinder binder(ShaderTrait::MapTexture);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
        sw_cursor.texture->render(QRegion(cursorRect), cursorRect);
        sw_cursor.texture->unbind();

        cursor->mark_as_rendered();

        glDisable(GL_BLEND);
    }

    void paintBackground(QRegion region) override
    {
        PaintClipper pc(region);
        if (!PaintClipper::clip()) {
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }
        if (pc.clip() && pc.paintArea().isEmpty())
            return; // no background to paint
        QVector<float> verts;
        for (PaintClipper::Iterator iterator; !iterator.isDone(); iterator.next()) {
            QRect r = iterator.boundingRect();
            verts << r.x() + r.width() << r.y();
            verts << r.x() << r.y();
            verts << r.x() << r.y() + r.height();
            verts << r.x() << r.y() + r.height();
            verts << r.x() + r.width() << r.y() + r.height();
            verts << r.x() + r.width() << r.y();
        }
        doPaintBackground(verts);
    }

    void extendPaintRegion(QRegion& region, bool opaqueFullscreen) override
    {
        if (m_backend->supportsBufferAge())
            return;

        if (kwinApp()->operationMode() == Application::OperationModeX11
            && GLPlatform::instance()->driver() == Driver_NVidia) {
            // Nvidia's X11 driver supports fast full buffer copies. So no need to extend damage.
            // TODO: Do we really need to check this here? Could we just run it anyway or does maybe
            //       not even reach this when we run Nvidia?
            return;
        }

        auto const& screenSize = kwinApp()->get_base().topology.size;
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

    QMatrix4x4 transformation(paint_type mask, const ScreenPaintData& data) const
    {
        QMatrix4x4 matrix;

        if (!(mask & paint_type::screen_transformed)) {
            return matrix;
        }

        matrix.translate(data.translation());
        const QVector3D scale = data.scale();
        matrix.scale(scale.x(), scale.y(), scale.z());

        if (data.rotationAngle() == 0.0)
            return matrix;

        // Apply the rotation
        // cannot use data.rotation->applyTo(&matrix) as QGraphicsRotation uses projectedRotate to
        // map back to 2D
        matrix.translate(data.rotationOrigin());
        const QVector3D axis = data.rotationAxis();
        matrix.rotate(data.rotationAngle(), axis.x(), axis.y(), axis.z());
        matrix.translate(-data.rotationOrigin());

        return matrix;
    }

    void paintDesktop(int desktop,
                      paint_type mask,
                      const QRegion& region,
                      ScreenPaintData& data) override
    {
        const QRect r = region.boundingRect();
        glEnable(GL_SCISSOR_TEST);
        glScissor(r.x(),
                  kwinApp()->get_base().topology.size.height() - r.y() - r.height(),
                  r.width(),
                  r.height());
        render::scene<Platform>::paintDesktop(desktop, mask, region, data);
        glDisable(GL_SCISSOR_TEST);
    }

    void paintEffectQuickView(EffectQuickView* w) override
    {
        GLTexture* t = w->bufferAsTexture();
        if (!t) {
            return;
        }

        ShaderTraits traits = ShaderTrait::MapTexture;
        auto a = w->opacity();
        if (a != 1.0) {
            traits |= ShaderTrait::Modulate;
        }

        auto shader = ShaderManager::instance()->pushShader(traits);
        auto const rect = w->geometry();

        QMatrix4x4 mvp(projectionMatrix());
        mvp.translate(rect.x(), rect.y());
        shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);

        if (a != 1.0) {
            shader->setUniform(GLShader::ModulationConstant, QVector4D(a, a, a, a));
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        t->bind();
        t->render(w->geometry());
        t->unbind();
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
            this, [this] { this->platform.compositor->reinitialize(); }, Qt::QueuedConnection);

        KNotification::event(QStringLiteral("graphicsreset"),
                             i18n("Desktop effects were restarted due to a graphics reset"));
    }

    void doPaintBackground(const QVector<float>& vertices)
    {
        GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setColor(QColor(0, 0, 0, 0));
        vbo->setData(vertices.count() / 2, 2, vertices.data(), nullptr);

        ShaderBinder binder(ShaderTrait::UniformColor);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, m_projectionMatrix);

        vbo->render(GL_TRIANGLES);
    }

    void updateProjectionMatrix()
    {
        m_projectionMatrix = createProjectionMatrix();
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

    std::deque<typename window_t::ref_t*>
    get_leads(std::deque<typename window_t::ref_t*> const& windows)
    {
        std::deque<typename window_t::ref_t*> leads;

        for (auto const& window : windows) {
            if (window->transient()->lead() && window->transient()->annexed) {
                auto const damage = window->damage_region;
                if (damage.isEmpty()) {
                    continue;
                }
                auto lead = win::lead_of_annexed_transient(window);
                auto const lead_render_geo = win::render_geometry(lead);
                auto const lead_damage = damage.translated(win::render_geometry(window).topLeft()
                                                           - lead_render_geo.topLeft());

                lead->repaints_region += lead_damage.translated(lead_render_geo.topLeft()
                                                                - lead->frameGeometry().topLeft());
                lead->damage_region += lead_damage;

                for (auto const& rect : lead_damage) {
                    // Emit for thumbnail repaint.
                    Q_EMIT lead->qobject->damaged(rect);
                }
            } else {
                leads.push_back(window);
            }
        }

        return leads;
    }

    void
    performPaintWindow(effect_window_t* w, paint_type mask, QRegion region, WindowPaintData& data)
    {
        if (flags(mask & paint_type::window_lanczos)) {
            if (!lanczos) {
                lanczos = new lanczos_filter<scene>(this);
            }
            lanczos->performPaint(w, mask, region, data);
        } else
            w->window.performPaint(mask, region, data);
    }

    QMatrix4x4 createProjectionMatrix() const
    {
        // Create a perspective projection with a 60° field-of-view,
        // and an aspect ratio of 1.0.
        const float fovY = 60.0f;
        const float aspect = 1.0f;
        const float zNear = 0.1f;
        const float zFar = 100.0f;

        const float yMax = zNear * std::tan(fovY * M_PI / 360.0f);
        const float yMin = -yMax;
        const float xMin = yMin * aspect;
        const float xMax = yMax * aspect;

        QMatrix4x4 projection;
        projection.frustum(xMin, xMax, yMin, yMax, zNear, zFar);

        // Create a second matrix that transforms screen coordinates
        // to world coordinates.
        const float scaleFactor = 1.1 * std::tan(fovY * M_PI / 360.0f) / yMax;

        QMatrix4x4 matrix;
        auto const& space_size = kwinApp()->get_base().topology.size;
        matrix.translate(xMin * scaleFactor, yMax * scaleFactor, -1.1);
        matrix.scale((xMax - xMin) * scaleFactor / space_size.width(),
                     -(yMax - yMin) * scaleFactor / space_size.height(),
                     0.001);

        // Combine the matrices
        return m_backend->transformation * projection * matrix;
    }

    backend_t* m_backend;
    SyncManager* m_syncManager{nullptr};
    SyncObject* m_currentFence{nullptr};

    lanczos_filter<type>* lanczos{nullptr};

    struct {
        std::unique_ptr<GLTexture> texture;
        bool dirty{true};
        QMetaObject::Connection notifier;
    } sw_cursor;

    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_screenProjectionMatrix;
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

    platform.createOpenGLSafePoint(OpenGLSafePoint::PreInit);
    auto post = [&] { platform.createOpenGLSafePoint(OpenGLSafePoint::PostInit); };

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
