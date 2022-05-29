/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

Based on glcompmgr code by Felix Bellaby.
Using code from Compiz and Beryl.

Explicit command stream synchronization based on the sample
implementation by James Jones <jajones@nvidia.com>,

Copyright © 2011 NVIDIA Corporation

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
#include "scene.h"

#include "deco_renderer.h"
#include "effect_frame.h"
#include "lanczos_filter.h"
#include "shadow.h"
#include "texture.h"
#include "window.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "input/cursor.h"
#include "main.h"
#include "render/compositor.h"
#include "render/cursor.h"
#include "render/effects.h"
#include "render/platform.h"
#include "win/deco/client_impl.h"
#include "win/geo.h"
#include "win/transient.h"

#include "kwineffects/paint_clipper.h"
#include <kwingl/platform.h>

#include <KLocalizedString>
#include <KNotification>
#include <QMatrix4x4>
#include <QPainter>
#include <stdexcept>
#include <unistd.h>
#include <xcb/sync.h>

namespace KWin::render::gl
{

extern int currentRefreshRate();

/**
 * SyncObject represents a fence used to synchronize operations in
 * the kwin command stream with operations in the X command stream.
 */
class SyncObject
{
public:
    enum State { Ready, TriggerSent, Waiting, Done, Resetting };

    SyncObject();
    ~SyncObject();

    State state() const
    {
        return m_state;
    }

    void trigger();
    void wait();
    bool finish();
    void reset();
    void finishResetting();

private:
    State m_state;
    GLsync m_sync;
    xcb_sync_fence_t m_fence;
    xcb_get_input_focus_cookie_t m_reset_cookie;
};

SyncObject::SyncObject()
{
    m_state = Ready;

    xcb_connection_t* const c = connection();

    m_fence = xcb_generate_id(c);
    xcb_sync_create_fence(c, rootWindow(), m_fence, false);
    xcb_flush(c);

    m_sync = glImportSyncEXT(GL_SYNC_X11_FENCE_EXT, m_fence, 0);
    m_reset_cookie.sequence = 0;
}

SyncObject::~SyncObject()
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

void SyncObject::trigger()
{
    Q_ASSERT(m_state == Ready || m_state == Resetting);

    // Finish resetting the fence if necessary
    if (m_state == Resetting)
        finishResetting();

    xcb_sync_trigger_fence(connection(), m_fence);
    m_state = TriggerSent;
}

void SyncObject::wait()
{
    if (m_state != TriggerSent)
        return;

    glWaitSync(m_sync, 0, GL_TIMEOUT_IGNORED);
    m_state = Waiting;
}

bool SyncObject::finish()
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

void SyncObject::reset()
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

void SyncObject::finishResetting()
{
    Q_ASSERT(m_state == Resetting);
    free(xcb_get_input_focus_reply(connection(), m_reset_cookie, nullptr));
    m_state = Ready;
}

// -----------------------------------------------------------------------

/**
 * SyncManager manages a set of fences used for explicit synchronization
 * with the X command stream.
 */
class SyncManager
{
public:
    enum { MaxFences = 4 };

    SyncManager();
    ~SyncManager();

    SyncObject* nextFence();
    bool updateFences();

private:
    std::array<SyncObject, MaxFences> m_fences;
    int m_next;
};

SyncManager::SyncManager()
    : m_next(0)
{
}

SyncManager::~SyncManager()
{
}

SyncObject* SyncManager::nextFence()
{
    SyncObject* fence = &m_fences[m_next];
    m_next = (m_next + 1) % MaxFences;
    return fence;
}

bool SyncManager::updateFences()
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

// -----------------------------------------------------------------------

/************************************************
 * scene
 ***********************************************/

scene::scene(render::gl::backend* backend, render::compositor& compositor)
    : render::scene(compositor)
    , m_backend(backend)
{
    if (!viewportLimitsMatched(kwinApp()->get_base().topology.size)) {
        // TODO(romangg): throw?
        return;
    }

    GLPlatform* glPlatform = GLPlatform::instance();

    // set strict binding
    if (kwinApp()->options->isGlStrictBindingFollowsDriver()) {
        kwinApp()->options->setGlStrictBinding(!glPlatform->supports(LooseBinding));
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
        qCDebug(KWIN_CORE) << "OpenGL 2.0 is not supported";
        init_ok = false;
        return;
    }

    // It is not legal to not have a vertex array object bound in a core context
    if (!GLPlatform::instance()->isGLES()
        && hasGLExtension(QByteArrayLiteral("GL_ARB_vertex_array_object"))) {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
    }

    qCDebug(KWIN_CORE) << "OpenGL 2 compositing successfully initialized";
    init_ok = true;
}

scene::~scene()
{
    if (init_ok || lanczos) {
        makeOpenGLContextCurrent();
    }

    // Need to reset early, otherwise the GL context is gone.
    sw_cursor.texture.reset();

    if (lanczos) {
        delete lanczos;
        lanczos = nullptr;
    }

    effect_frame::cleanup();

    delete m_syncManager;
}

CompositingType scene::compositingType() const
{
    return OpenGLCompositing;
}

bool scene::hasSwapEvent() const
{
    return m_backend->hasSwapEvent();
}

void scene::idle()
{
    m_backend->idle();
    render::scene::idle();
}

bool scene::initFailed() const
{
    return !init_ok;
}

void scene::handleGraphicsReset(GLenum status)
{
    switch (status) {
    case GL_GUILTY_CONTEXT_RESET:
        qCDebug(KWIN_CORE) << "A graphics reset attributable to the current GL context occurred.";
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
        this, [this] { compositor.reinitialize(); }, Qt::QueuedConnection);

    KNotification::event(QStringLiteral("graphicsreset"),
                         i18n("Desktop effects were restarted due to a graphics reset"));
}

void scene::triggerFence()
{
    if (m_syncManager) {
        m_currentFence = m_syncManager->nextFence();
        m_currentFence->trigger();
    }
}

QMatrix4x4 scene::projectionMatrix() const
{
    return m_projectionMatrix;
}

QMatrix4x4 scene::screenProjectionMatrix() const
{
    return m_screenProjectionMatrix;
}

void scene::insertWait()
{
    if (m_currentFence && m_currentFence->state() != SyncObject::Waiting) {
        m_currentFence->wait();
    }
}

/**
 * Render cursor texture in case hardware cursor is disabled.
 * Useful for screen recording apps or backends that can't do planes.
 */
void scene::paintCursor()
{
    auto cursor = compositor.software_cursor.get();

    // don't paint if we use hardware cursor or the cursor is hidden
    if (!cursor->enabled || kwinApp()->input->cursor->is_hidden() || cursor->image().isNull()) {
        return;
    }

    // lazy init texture cursor only in case we need software rendering
    if (sw_cursor.dirty) {
        auto const img = compositor.software_cursor->image();

        // If there was no new image we are still dirty and try to update again next paint cycle.
        sw_cursor.dirty = img.isNull();

        // With an image we update the texture, or if one was never set before create a default one.
        if (!img.isNull() || !sw_cursor.texture) {
            sw_cursor.texture.reset(new GLTexture(img));
        }

        // handle shape update on case cursor image changed
        if (!sw_cursor.notifier) {
            sw_cursor.notifier = connect(
                cursor, &render::cursor::changed, this, [this] { sw_cursor.dirty = true; });
        }
    }

    // get cursor position in projection coordinates
    auto const cursorPos = input::get_cursor()->pos() - cursor->hotspot();
    auto const cursorRect = QRect(0, 0, sw_cursor.texture->width(), sw_cursor.texture->height());
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

int64_t scene::paint(QRegion damage,
                     std::deque<Toplevel*> const& toplevels,
                     std::chrono::milliseconds presentTime)
{
    // Remove all subordinate transients. These are painted as part of their leads.
    // TODO: Optimize this by *not* painting them as part of their leads if no quad transforming
    //       (and opacity changing or animated?) effects are active.
    auto const leads = get_leads(toplevels);
    createStackingOrder(leads);

    // After this call, update will contain the damaged region in the back buffer. This is the
    // region that needs to be posted to repair the front buffer. It doesn't include the additional
    // damage returned by prepareRenderingFrame(). The valid region is the region that has been
    // repainted, and may be larger than the update region.
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

    setRenderTargetRect(QRect({}, kwinApp()->get_base().topology.size));
    setRenderTargetScale(1.);

    auto mask = paint_type::none;
    updateProjectionMatrix();

    // Call generic implementation.
    paintScreen(mask, damage, repaint, &update, &valid, presentTime, projectionMatrix());

    if (!GLPlatform::instance()->isGLES()) {
        auto const displayRegion
            = QRegion(0, 0, renderTargetRect().width(), renderTargetRect().height());

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
            qCDebug(KWIN_CORE) << "Aborting explicit synchronization with the X command stream.";
            qCDebug(KWIN_CORE) << "Future frames will be rendered unsynchronized.";
            delete m_syncManager;
            m_syncManager = nullptr;
        }
        m_currentFence = nullptr;
    }

    clearStackingOrder();
    return m_backend->renderTime();
}

int64_t scene::paint_output(base::output* output,
                            QRegion damage,
                            std::deque<Toplevel*> const& windows,
                            std::chrono::milliseconds presentTime)
{
    createStackingOrder(get_leads(windows));

    // Trigger render timer start.
    m_backend->prepareRenderingFrame();

    // Makes context current on the output.
    auto const repaint = m_backend->prepareRenderingForScreen(output);
    GLVertexBuffer::streamingBuffer()->beginFrame();

    setRenderTargetRect(output->geometry());
    setRenderTargetScale(output->scale());

    GLenum const status = glGetGraphicsResetStatus();
    if (status != GL_NO_ERROR) {
        handleGraphicsReset(status);
        return 0;
    }

    updateProjectionMatrix();

    auto mask = paint_type::none;
    QRegion update;
    QRegion valid;
    repaint_output = output;

    // Call generic implementation.
    paintScreen(mask,
                damage.intersected(renderTargetRect()),
                repaint,
                &update,
                &valid,
                presentTime,
                projectionMatrix());
    paintCursor();

    GLVertexBuffer::streamingBuffer()->endOfFrame();
    m_backend->endRenderingFrameForScreen(output, valid, update);

    clearStackingOrder();
    repaint_output = nullptr;

    return m_backend->renderTime();
}

std::deque<Toplevel*> scene::get_leads(std::deque<Toplevel*> const& windows)
{
    std::deque<Toplevel*> leads;

    for (auto const& window : windows) {
        if (window->transient()->lead() && window->transient()->annexed) {
            auto const damage = window->damage();
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
                Q_EMIT lead->damaged(lead, rect);
            }
        } else {
            leads.push_back(window);
        }
    }

    return leads;
}

QMatrix4x4 scene::transformation(paint_type mask, const ScreenPaintData& data) const
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
    // cannot use data.rotation->applyTo(&matrix) as QGraphicsRotation uses projectedRotate to map
    // back to 2D
    matrix.translate(data.rotationOrigin());
    const QVector3D axis = data.rotationAxis();
    matrix.rotate(data.rotationAngle(), axis.x(), axis.y(), axis.z());
    matrix.translate(-data.rotationOrigin());

    return matrix;
}

void scene::paintBackground(QRegion region)
{
    PaintClipper pc(region);
    if (!PaintClipper::clip()) {
        glClearColor(0, 0, 0, 1);
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

void scene::extendPaintRegion(QRegion& region, bool opaqueFullscreen)
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
        //                 damagedPixels += r.width() * r.height(); // combined window damage test
        damagedPixels = r.width() * r.height(); // experimental single window damage testing
        if (damagedPixels > fullRepaintLimit) {
            region = displayRegion;
            return;
        }
    }
}

render::gl::texture* scene::createTexture()
{
    return new render::gl::texture(m_backend);
}

bool scene::viewportLimitsMatched(const QSize& size) const
{
    if (!windowing_integration.handle_viewport_limits_alarm) {
        // If we have now way of reacting to the alarm this check is useless.
        return true;
    }

    GLint limit[2];
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, limit);
    if (limit[0] < size.width() || limit[1] < size.height()) {
        windowing_integration.handle_viewport_limits_alarm();
        return false;
    }

    return true;
}

void scene::handle_screen_geometry_change(QSize const& size)
{
    if (!viewportLimitsMatched(size)) {
        return;
    }
    glViewport(0, 0, size.width(), size.height());
    m_backend->screenGeometryChanged(size);
}

void scene::paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data)
{
    const QRect r = region.boundingRect();
    glEnable(GL_SCISSOR_TEST);
    glScissor(r.x(),
              kwinApp()->get_base().topology.size.height() - r.y() - r.height(),
              r.width(),
              r.height());
    render::scene::paintDesktop(desktop, mask, region, data);
    glDisable(GL_SCISSOR_TEST);
}

void scene::paintEffectQuickView(EffectQuickView* w)
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

bool scene::makeOpenGLContextCurrent()
{
    return m_backend->makeCurrent();
}

void scene::doneOpenGLContextCurrent()
{
    m_backend->doneCurrent();
}

bool scene::supportsSurfacelessContext() const
{
    return m_backend->supportsSurfacelessContext();
}

render::effect_frame* scene::createEffectFrame(effect_frame_impl* frame)
{
    return new effect_frame(frame, this);
}

std::unique_ptr<render::shadow> scene::createShadow(Toplevel* toplevel)
{
    return std::make_unique<shadow>(toplevel, *this);
}

win::deco::renderer* scene::createDecorationRenderer(win::deco::client_impl* impl)
{
    return new deco_renderer(impl, *this);
}

bool scene::animationsSupported() const
{
    return !GLPlatform::instance()->isSoftwareEmulation();
}

QVector<QByteArray> scene::openGLPlatformInterfaceExtensions() const
{
    return m_backend->extensions().toVector();
}

bool scene::supported(render::gl::backend* backend)
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

QMatrix4x4 scene::createProjectionMatrix() const
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

void scene::updateProjectionMatrix()
{
    m_projectionMatrix = createProjectionMatrix();
}

void scene::paintSimpleScreen(paint_type mask, QRegion region)
{
    m_screenProjectionMatrix = m_projectionMatrix;

    render::scene::paintSimpleScreen(mask, region);
}

void scene::paintGenericScreen(paint_type mask, ScreenPaintData data)
{
    const QMatrix4x4 screenMatrix = transformation(mask, data);

    m_screenProjectionMatrix = m_projectionMatrix * screenMatrix;

    render::scene::paintGenericScreen(mask, data);
}

void scene::doPaintBackground(const QVector<float>& vertices)
{
    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setUseColor(true);
    vbo->setData(vertices.count() / 2, 2, vertices.data(), nullptr);

    ShaderBinder binder(ShaderTrait::UniformColor);
    binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, m_projectionMatrix);

    vbo->render(GL_TRIANGLES);
}

std::unique_ptr<render::window> scene::createWindow(Toplevel* t)
{
    return std::make_unique<window>(t, this);
}

void scene::finalDrawWindow(effects_window_impl* w,
                            paint_type mask,
                            QRegion region,
                            WindowPaintData& data)
{
    if (kwinApp()->is_screen_locked() && !w->window()->isLockScreen()
        && !w->window()->isInputMethod()) {
        return;
    }
    performPaintWindow(w, mask, region, data);
}

void scene::performPaintWindow(effects_window_impl* w,
                               paint_type mask,
                               QRegion region,
                               WindowPaintData& data)
{
    if (flags(mask & paint_type::window_lanczos)) {
        if (!lanczos) {
            lanczos = new lanczos_filter(this);
        }
        lanczos->performPaint(w, mask, region, data);
    } else
        w->sceneWindow()->performPaint(mask, region, data);
}

backend* create_backend(render::compositor& compositor)
{
    try {
        return compositor.platform.createOpenGLBackend(compositor);
    } catch (std::runtime_error& error) {
        qCWarning(KWIN_CORE) << "Creating OpenGL backend failed:" << error.what();
        return nullptr;
    }
}

render::scene* create_scene_impl(render::compositor& compositor)
{
    auto backend = create_backend(compositor);
    if (!backend) {
        return nullptr;
    }

    gl::scene* scene{nullptr};

    // first let's try an OpenGL 2 scene
    if (scene::supported(backend)) {
        scene = new gl::scene(backend, compositor);
        if (scene->initFailed()) {
            delete scene;
            scene = nullptr;
        }
    }

    if (!scene) {
        if (GLPlatform::instance()->recommendedCompositor() == XRenderCompositing) {
            qCCritical(KWIN_CORE)
                << "OpenGL driver recommends XRender based compositing. Falling back to XRender.";
            qCCritical(KWIN_CORE)
                << "To overwrite the detection use the environment variable KWIN_COMPOSE";
            qCCritical(KWIN_CORE)
                << "For more information see "
                   "https://community.kde.org/KWin/Environment_Variables#KWIN_COMPOSE";
        }
        compositor.platform.render_stop(false);
    }

    return scene;
}

render::scene* create_scene(render::compositor& compositor)
{
    qCDebug(KWIN_CORE) << "Creating OpenGL scene.";

    // Some broken drivers crash on glXQuery() so to prevent constant KWin crashes:
    if (compositor.platform.openGLCompositingIsBroken()) {
        qCWarning(KWIN_CORE) << "KWin has detected that your OpenGL library is unsafe to use";
        return nullptr;
    }

    compositor.platform.createOpenGLSafePoint(OpenGLSafePoint::PreInit);
    auto scene = create_scene_impl(compositor);
    compositor.platform.createOpenGLSafePoint(OpenGLSafePoint::PostInit);

    return scene;
}

}
