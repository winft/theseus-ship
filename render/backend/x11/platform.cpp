/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "glx.h"

#include <config-kwin.h>
#include <kwinconfig.h>

#include "deco_renderer.h"
#include "effects.h"
#include "non_composited_outline.h"
#include "output.h"
#include "output_helpers.h"
#include "x11_logging.h"

#if HAVE_EPOXY_GLX
#include "glx_backend.h"
#endif

#include "base/options.h"
#include "base/output_helpers.h"
#include "base/x11/output.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/randr.h"
#include "kwinxrenderutils.h"
#include "main_x11.h"
#include "randr_filter.h"
#include "render/compositor.h"
#include "screens.h"
#include "toplevel.h"
#include "win/space.h"

#include <KConfigGroup>
#include <KCrash>
#include <KLocalizedString>

#include <QOpenGLContext>
#include <QThread>
#include <QX11Info>

namespace KWin::render::backend::x11
{

/**
 * Tests whether GLX is supported and returns @c true
 * in case KWin is compiled with OpenGL support and GLX
 * is available.
 *
 * If KWin is compiled with OpenGL ES or without OpenGL at
 * all, @c false is returned.
 * @returns @c true if GLX is available, @c false otherwise and if not build with OpenGL
 * support.
 */
static bool has_glx()
{
    return base::x11::xcb::extensions::self()->has_glx();
}

platform::platform(base::x11::platform& base)
    : render::platform(base)
    , m_x11Display(QX11Info::display())
    , base{base}
{
}

platform::~platform()
{
    if (m_openGLFreezeProtectionThread) {
        m_openGLFreezeProtectionThread->quit();
        m_openGLFreezeProtectionThread->wait();
        delete m_openGLFreezeProtectionThread;
    }
    XRenderUtils::cleanup();
}

void platform::init()
{
    if (!QX11Info::isPlatformX11()) {
        throw std::exception();
    }

    update_outputs_impl<base::x11::xcb::randr::screen_resources>();

    XRenderUtils::init(kwinApp()->x11Connection(), kwinApp()->x11RootWindow());
    m_randrFilter.reset(new RandrFilter(this));
}

gl::backend* platform::createOpenGLBackend(render::compositor& compositor)
{
    if (gl_backend) {
        start_glx_backend(m_x11Display, compositor, *gl_backend);
        return gl_backend.get();
    }

    switch (kwinApp()->options->glPlatformInterface()) {
#if HAVE_EPOXY_GLX
    case GlxPlatformInterface:
        if (has_glx()) {
            gl_backend = std::make_unique<glx_backend>(m_x11Display, compositor);
            return gl_backend.get();
        } else {
            qCWarning(KWIN_X11) << "Glx not available, trying EGL instead.";
            // no break, needs fall-through
            Q_FALLTHROUGH();
        }
#endif
    case EglPlatformInterface:
    default:
        // no backend available
        return nullptr;
    }
}

void platform::render_stop(bool /*on_shutdown*/)
{
    assert(gl_backend);
    tear_down_glx_backend(*gl_backend);
}

bool platform::requiresCompositing() const
{
    return false;
}

bool platform::openGLCompositingIsBroken() const
{
    const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
    return KConfigGroup(kwinApp()->config(), "Compositing").readEntry(unsafeKey, false);
}

QString platform::compositingNotPossibleReason() const
{
    // first off, check whether we figured that we'll crash on detection because of a buggy driver
    KConfigGroup gl_workaround_group(kwinApp()->config(), "Compositing");
    const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
    if (gl_workaround_group.readEntry("Backend", "OpenGL") == QLatin1String("OpenGL")
        && gl_workaround_group.readEntry(unsafeKey, false))
        return i18n(
            "<b>OpenGL compositing (the default) has crashed KWin in the past.</b><br>"
            "This was most likely due to a driver bug."
            "<p>If you think that you have meanwhile upgraded to a stable driver,<br>"
            "you can reset this protection but <b>be aware that this might result in an immediate "
            "crash!</b></p>"
            "<p>Alternatively, you might want to use the XRender backend instead.</p>");

    if (!base::x11::xcb::extensions::self()->is_composite_available()
        || !base::x11::xcb::extensions::self()->is_damage_available()) {
        return i18n("Required X extensions (XComposite and XDamage) are not available.");
    }
#if !defined(KWIN_HAVE_XRENDER_COMPOSITING)
    if (!has_glx())
        return i18n("GLX/OpenGL are not available and only OpenGL support is compiled.");
#else
    if (!(has_glx()
          || (base::x11::xcb::extensions::self()->is_render_available()
              && base::x11::xcb::extensions::self()->is_fixes_available()))) {
        return i18n("GLX/OpenGL and XRender/XFixes are not available.");
    }
#endif
    return QString();
}

bool platform::compositingPossible() const
{
    // first off, check whether we figured that we'll crash on detection because of a buggy driver
    KConfigGroup gl_workaround_group(kwinApp()->config(), "Compositing");
    const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
    if (gl_workaround_group.readEntry("Backend", "OpenGL") == QLatin1String("OpenGL")
        && gl_workaround_group.readEntry(unsafeKey, false))
        return false;

    if (!base::x11::xcb::extensions::self()->is_composite_available()) {
        qCDebug(KWIN_X11) << "No composite extension available";
        return false;
    }
    if (!base::x11::xcb::extensions::self()->is_damage_available()) {
        qCDebug(KWIN_X11) << "No damage extension available";
        return false;
    }
    if (has_glx())
        return true;
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (base::x11::xcb::extensions::self()->is_render_available()
        && base::x11::xcb::extensions::self()->is_fixes_available()) {
        return true;
    }
#endif
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        return true;
    } else if (qstrcmp(qgetenv("KWIN_COMPOSE"), "O2ES") == 0) {
        return true;
    }
    qCDebug(KWIN_X11) << "No OpenGL or XRender/XFixes support";
    return false;
}

void platform::createOpenGLSafePoint(OpenGLSafePoint safePoint)
{
    const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
    auto group = KConfigGroup(kwinApp()->config(), "Compositing");
    switch (safePoint) {
    case OpenGLSafePoint::PreInit:
        group.writeEntry(unsafeKey, true);
        group.sync();
        // Deliberately continue with PreFrame
        Q_FALLTHROUGH();
    case OpenGLSafePoint::PreFrame:
        if (m_openGLFreezeProtectionThread == nullptr) {
            Q_ASSERT(m_openGLFreezeProtection == nullptr);
            m_openGLFreezeProtectionThread = new QThread(this);
            m_openGLFreezeProtectionThread->setObjectName("FreezeDetector");
            m_openGLFreezeProtectionThread->start();
            m_openGLFreezeProtection = new QTimer;
            m_openGLFreezeProtection->setInterval(15000);
            m_openGLFreezeProtection->setSingleShot(true);
            m_openGLFreezeProtection->start();
            const QString configName = kwinApp()->config()->name();
            m_openGLFreezeProtection->moveToThread(m_openGLFreezeProtectionThread);
            connect(
                m_openGLFreezeProtection,
                &QTimer::timeout,
                m_openGLFreezeProtection,
                [configName] {
                    const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
                    auto group = KConfigGroup(KSharedConfig::openConfig(configName), "Compositing");
                    group.writeEntry(unsafeKey, true);
                    group.sync();
                    KCrash::setDrKonqiEnabled(false);
                    qFatal("Freeze in OpenGL initialization detected");
                },
                Qt::DirectConnection);
        } else {
            Q_ASSERT(m_openGLFreezeProtection);
            QMetaObject::invokeMethod(m_openGLFreezeProtection, "start", Qt::QueuedConnection);
        }
        break;
    case OpenGLSafePoint::PostInit:
        group.writeEntry(unsafeKey, false);
        group.sync();
        // Deliberately continue with PostFrame
        Q_FALLTHROUGH();
    case OpenGLSafePoint::PostFrame:
        QMetaObject::invokeMethod(m_openGLFreezeProtection, "stop", Qt::QueuedConnection);
        break;
    case OpenGLSafePoint::PostLastGuardedFrame:
        m_openGLFreezeProtection->deleteLater();
        m_openGLFreezeProtection = nullptr;
        m_openGLFreezeProtectionThread->quit();
        m_openGLFreezeProtectionThread->wait();
        delete m_openGLFreezeProtectionThread;
        m_openGLFreezeProtectionThread = nullptr;
        break;
    }
}

outline_visual* platform::createOutline(render::outline* outline)
{
    // first try composited Outline
    auto ret = render::platform::createOutline(outline);
    if (!ret) {
        ret = new non_composited_outline(outline);
    }
    return ret;
}

Decoration::Renderer* platform::createDecorationRenderer(Decoration::DecoratedClientImpl* client)
{
    auto renderer = render::platform::createDecorationRenderer(client);
    if (!renderer) {
        renderer = new deco_renderer(client);
    }
    return renderer;
}

void platform::invertScreen()
{
    bool succeeded = false;

    if (base::x11::xcb::extensions::self()->is_randr_available()) {
        const auto active_client = workspace()->activeClient();
        base::x11::xcb::randr::screen_resources res(
            (active_client && active_client->xcb_window() != XCB_WINDOW_NONE)
                ? active_client->xcb_window()
                : rootWindow());

        if (!res.is_null()) {
            for (int j = 0; j < res->num_crtcs; ++j) {
                auto crtc = res.crtcs()[j];
                base::x11::xcb::randr::crtc_gamma gamma(crtc);
                if (gamma.is_null()) {
                    continue;
                }
                if (gamma->size) {
                    qCDebug(KWIN_X11) << "inverting screen using xcb_randr_set_crtc_gamma";
                    const int half = gamma->size / 2 + 1;

                    uint16_t* red = gamma.red();
                    uint16_t* green = gamma.green();
                    uint16_t* blue = gamma.blue();
                    for (int i = 0; i < half; ++i) {
                        auto invert = [&gamma, i](uint16_t* ramp) {
                            qSwap(ramp[i], ramp[gamma->size - 1 - i]);
                        };
                        invert(red);
                        invert(green);
                        invert(blue);
                    }
                    xcb_randr_set_crtc_gamma(connection(), crtc, gamma->size, red, green, blue);
                    succeeded = true;
                }
            }
        }
    }
    if (!succeeded) {
        render::platform::invertScreen();
    }
}

void platform::createEffectsHandler(render::compositor* compositor, render::scene* scene)
{
    new effects_handler_impl(compositor, scene);
}

QVector<CompositingType> platform::supportedCompositors() const
{
    QVector<CompositingType> compositors;
#if HAVE_EPOXY_GLX
    compositors << OpenGLCompositing;
#endif
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    compositors << XRenderCompositing;
#endif
    compositors << NoCompositing;
    return compositors;
}

void platform::update_outputs()
{
    update_outputs_impl<base::x11::xcb::randr::current_resources>();
}

template<typename Resources>
void platform::update_outputs_impl()
{
    auto outputs = get_outputs(base, Resources(rootWindow()));

    qCDebug(KWIN_X11) << "Update outputs:" << base.outputs.size() << "-->" << outputs.size();

    // First check for removed outputs (we go backwards through the outputs, LIFO).
    for (auto old_it = base.outputs.rbegin(); old_it != base.outputs.rend();) {
        auto x11_old_out = static_cast<base::x11::output*>(old_it->get());

        auto is_in_new_outputs = [x11_old_out, &outputs] {
            auto it = std::find_if(outputs.begin(), outputs.end(), [x11_old_out](auto const& out) {
                return x11_old_out->data.crtc == out->data.crtc
                    && x11_old_out->data.name == out->data.name;
            });
            return it != outputs.end();
        };

        if (is_in_new_outputs()) {
            // The old output is still there. Keep it in the base outputs.
            old_it++;
            continue;
        }

        qCDebug(KWIN_X11) << "  removed:" << x11_old_out->name();
        auto old_out = std::move(*old_it);
        old_it = static_cast<decltype(old_it)>(base.outputs.erase(std::next(old_it).base()));
        Q_EMIT base.output_removed(old_out.get());
    }

    // Second check for added outputs.
    for (auto& out : outputs) {
        auto it
            = std::find_if(base.outputs.begin(), base.outputs.end(), [&out](auto const& old_out) {
                  auto old_x11_out = static_cast<base::x11::output*>(old_out.get());
                  return old_x11_out->data.crtc == out->data.crtc
                      && old_x11_out->data.name == out->data.name;
              });
        if (it == base.outputs.end()) {
            qCDebug(KWIN_X11) << "  added:" << out->name();
            base.outputs.push_back(std::move(out));
            Q_EMIT base.output_added(base.outputs.back().get());
        }
    }

    base::update_output_topology(base);
}

}
