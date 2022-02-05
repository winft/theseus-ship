/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

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
#include "x11_logging.h"

#if HAVE_EPOXY_GLX
#include "glx_backend.h"
#endif

#include "base/x11/output.h"
#include "main_x11.h"
#include "options.h"
#include "randr_filter.h"
#include "render/compositor.h"
#include "screens.h"
#include "toplevel.h"
#include "workspace.h"
#include "xcbutils.h"

#include <kwinxrenderutils.h>

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

    initOutputs();
    base.screens.updateAll();

    connect(&base.screens, &Screens::changed, this, [] {
        if (!workspace()->compositing()) {
            return;
        }
        // desktopResized() should take care of when the size or
        // shape of the desktop has changed, but we also want to
        // catch refresh rate changes
        //
        // TODO: is this still necessary since we get the maximal refresh rate now dynamically?
        render::compositor::self()->reinitialize();
    });

    XRenderUtils::init(kwinApp()->x11Connection(), kwinApp()->x11RootWindow());
    m_randrFilter.reset(new RandrFilter(this));
}

gl::backend* platform::createOpenGLBackend(render::compositor& compositor)
{
    if (gl_backend) {
        start_glx_backend(m_x11Display, compositor, *gl_backend);
        return gl_backend.get();
    }

    switch (options->glPlatformInterface()) {
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

void platform::initOutputs()
{
    doUpdateOutputs<base::x11::xcb::randr::screen_resources>();
}

void platform::updateOutputs()
{
    doUpdateOutputs<base::x11::xcb::randr::current_resources>();
}

template<typename T>
void platform::doUpdateOutputs()
{
    auto fallback = [this]() {
        auto o = std::make_unique<base::x11::output>(base);
        o->data.gamma_ramp_size = 0;
        o->data.refresh_rate = -1.0f;
        o->data.name = QStringLiteral("Fallback");
        base.outputs.push_back(std::move(o));
    };

    // TODO: instead of resetting all outputs, check if new output is added/removed
    //       or still available and leave still available outputs in m_outputs
    //       untouched (like in DRM backend)
    base.outputs.clear();

    if (!base::x11::xcb::extensions::self()->is_randr_available()) {
        fallback();
        return;
    }
    T resources(rootWindow());
    if (resources.is_null()) {
        fallback();
        return;
    }
    xcb_randr_crtc_t* crtcs = resources.crtcs();
    xcb_randr_mode_info_t* modes = resources.modes();

    QVector<base::x11::xcb::randr::crtc_info> infos(resources->num_crtcs);
    for (int i = 0; i < resources->num_crtcs; ++i) {
        infos[i] = base::x11::xcb::randr::crtc_info(crtcs[i], resources->config_timestamp);
    }

    for (int i = 0; i < resources->num_crtcs; ++i) {
        base::x11::xcb::randr::crtc_info info(infos.at(i));

        xcb_randr_output_t* outputs = info.outputs();
        QVector<base::x11::xcb::randr::output_info> outputInfos(outputs ? resources->num_outputs
                                                                        : 0);
        if (outputs) {
            for (int i = 0; i < resources->num_outputs; ++i) {
                outputInfos[i]
                    = base::x11::xcb::randr::output_info(outputs[i], resources->config_timestamp);
            }
        }

        float refreshRate = -1.0f;
        for (int j = 0; j < resources->num_modes; ++j) {
            if (info->mode == modes[j].id) {
                if (modes[j].htotal != 0 && modes[j].vtotal != 0) { // BUG 313996
                    // refresh rate calculation - WTF was wikipedia 1998 when I needed it?
                    int dotclock = modes[j].dot_clock, vtotal = modes[j].vtotal;
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE)
                        dotclock *= 2;
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN)
                        vtotal *= 2;
                    refreshRate = dotclock / float(modes[j].htotal * vtotal);
                }
                break; // found mode
            }
        }

        const QRect geo = info.rect();
        if (geo.isValid()) {
            xcb_randr_crtc_t crtc = crtcs[i];

            // TODO: Perhaps the output has to save the inherited gamma ramp and
            // restore it during tear down. Currently neither standalone x11 nor
            // drm platform do this.
            base::x11::xcb::randr::crtc_gamma gamma(crtc);

            auto o = std::make_unique<base::x11::output>(base);
            o->data.crtc = crtc;
            o->data.gamma_ramp_size = gamma.is_null() ? 0 : gamma->size;
            o->data.geometry = geo;
            o->data.refresh_rate = refreshRate * 1000;

            for (int j = 0; j < info->num_outputs; ++j) {
                base::x11::xcb::randr::output_info outputInfo(outputInfos.at(j));
                if (outputInfo->crtc != crtc) {
                    continue;
                }
                QSize physicalSize(outputInfo->mm_width, outputInfo->mm_height);
                switch (info->rotation) {
                case XCB_RANDR_ROTATION_ROTATE_0:
                case XCB_RANDR_ROTATION_ROTATE_180:
                    break;
                case XCB_RANDR_ROTATION_ROTATE_90:
                case XCB_RANDR_ROTATION_ROTATE_270:
                    physicalSize.transpose();
                    break;
                case XCB_RANDR_ROTATION_REFLECT_X:
                case XCB_RANDR_ROTATION_REFLECT_Y:
                    break;
                }
                o->data.name = outputInfo.name();
                o->data.physical_size = physicalSize;
                break;
            }

            base.outputs.push_back(std::move(o));
        }
    }

    if (base.outputs.empty()) {
        fallback();
    }
}

}
