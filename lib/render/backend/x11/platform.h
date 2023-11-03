/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco_renderer.h"
#include "glx.h"
#include "non_composited_outline.h"

#if HAVE_EPOXY_GLX
#include "glx_backend.h"
#endif

#include "base/x11/xcb/randr.h"
#include "render/x11/platform.h"

#include <KCrash>
#include <QObject>
#include <memory>

#include <X11/Xlib-xcb.h>
#include <base/x11/fixx11h.h>

namespace KWin::render::backend::x11
{

template<typename Base>
class platform : public render::x11::platform<Base>
{
public:
    using type = platform<Base>;
    using abstract_type = render::x11::platform<Base>;
    using scene_t = typename abstract_type::scene_t;

    platform(Base& base)
        : render::x11::platform<Base>(base)
        , m_x11Display(QX11Info::display())
    {
    }

    ~platform() override
    {
        if (m_openGLFreezeProtectionThread) {
            m_openGLFreezeProtectionThread->quit();
            m_openGLFreezeProtectionThread->wait();
            m_openGLFreezeProtectionThread.reset();
        }
        XRenderUtils::cleanup();

        // TODO(romangg): Should be in abstract platform. Still needs the gl backend though.
        Q_EMIT this->qobject->aboutToDestroy();
        compositor_stop(*this, true);
    }

    void init()
    {
        if (!QX11Info::isPlatformX11()) {
            throw std::exception();
        }

        XRenderUtils::init(this->base.x11_data.connection, this->base.x11_data.root_window);
    }

    gl::backend<gl::scene<abstract_type>, abstract_type>* get_opengl_backend() override
    {
        if (gl_backend) {
            start_glx_backend(m_x11Display, *gl_backend);
            return gl_backend.get();
        }

#if HAVE_EPOXY_GLX
        if (has_glx()) {
            gl_backend = std::make_unique<glx_backend<type>>(m_x11Display, *this);
            return gl_backend.get();
        }
#endif
        throw std::runtime_error("GLX backend not available.");
    }

    void render_stop(bool /*on_shutdown*/) override
    {
        if (gl_backend) {
            tear_down_glx_backend(*gl_backend);
            gl_backend.reset();
        }
    }

    bool compositingPossible() const override
    {
        // first off, check whether we figured that we'll crash on detection because of a buggy
        // driver
        KConfigGroup gl_workaround_group(this->base.config.main, "Compositing");
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        if (gl_workaround_group.readEntry("Backend", "OpenGL") == QLatin1String("OpenGL")
            && gl_workaround_group.readEntry(unsafeKey, false))
            return false;

        if (!base::x11::xcb::extensions::self()->is_composite_available()) {
            qCDebug(KWIN_CORE) << "No composite extension available";
            return false;
        }
        if (!base::x11::xcb::extensions::self()->is_damage_available()) {
            qCDebug(KWIN_CORE) << "No damage extension available";
            return false;
        }
        if (has_glx())
            return true;
        if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
            return true;
        } else if (qstrcmp(qgetenv("KWIN_COMPOSE"), "O2ES") == 0) {
            return true;
        }
        qCDebug(KWIN_CORE) << "No OpenGL support";
        return false;
    }

    QString compositingNotPossibleReason() const override
    {
        // first off, check whether we figured that we'll crash on detection because of a buggy
        // driver
        KConfigGroup gl_workaround_group(this->base.config.main, "Compositing");
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        if (gl_workaround_group.readEntry("Backend", "OpenGL") == QLatin1String("OpenGL")
            && gl_workaround_group.readEntry(unsafeKey, false))
            return i18n(
                "<b>OpenGL compositing (the default) has crashed KWin in the past.</b><br>"
                "This was most likely due to a driver bug."
                "<p>If you think that you have meanwhile upgraded to a stable driver,<br>"
                "you can reset this protection but <b>be aware that this might result in an "
                "immediate crash!</b></p>");

        if (!base::x11::xcb::extensions::self()->is_composite_available()
            || !base::x11::xcb::extensions::self()->is_damage_available()) {
            return i18n("Required X extensions (XComposite and XDamage) are not available.");
        }
        if (!has_glx())
            return i18n("GLX/OpenGL are not available and only OpenGL support is compiled.");
        return QString();
    }

    void createOpenGLSafePoint(opengl_safe_point safePoint) override
    {
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        auto group = KConfigGroup(this->base.config.main, "Compositing");
        switch (safePoint) {
        case opengl_safe_point::pre_init:
            group.writeEntry(unsafeKey, true);
            group.sync();
            // Deliberately continue with PreFrame
            Q_FALLTHROUGH();
        case opengl_safe_point::pre_frame:
            if (!m_openGLFreezeProtectionThread) {
                Q_ASSERT(m_openGLFreezeProtection == nullptr);
                m_openGLFreezeProtectionThread = std::make_unique<QThread>();
                m_openGLFreezeProtectionThread->setObjectName("FreezeDetector");
                m_openGLFreezeProtectionThread->start();
                m_openGLFreezeProtection = new QTimer;
                m_openGLFreezeProtection->setInterval(15000);
                m_openGLFreezeProtection->setSingleShot(true);
                m_openGLFreezeProtection->start();
                const QString configName = this->base.config.main->name();
                m_openGLFreezeProtection->moveToThread(m_openGLFreezeProtectionThread.get());
                QObject::connect(
                    m_openGLFreezeProtection,
                    &QTimer::timeout,
                    m_openGLFreezeProtection,
                    [configName] {
                        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
                        auto group
                            = KConfigGroup(KSharedConfig::openConfig(configName), "Compositing");
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
        case opengl_safe_point::post_init:
            group.writeEntry(unsafeKey, false);
            group.sync();
            // Deliberately continue with PostFrame
            Q_FALLTHROUGH();
        case opengl_safe_point::post_frame:
            QMetaObject::invokeMethod(m_openGLFreezeProtection, "stop", Qt::QueuedConnection);
            break;
        case opengl_safe_point::post_last_guarded_frame:
            m_openGLFreezeProtection->deleteLater();
            m_openGLFreezeProtection = nullptr;
            m_openGLFreezeProtectionThread->quit();
            m_openGLFreezeProtectionThread->wait();
            m_openGLFreezeProtectionThread.reset();
            break;
        }
    }

    outline_visual* create_non_composited_outline(render::outline* outline) override
    {
        return new non_composited_outline(this->base.x11_data, outline);
    }

    void invertScreen() override
    {
        // We prefer inversion via effects.
        if (this->effects && this->effects->invert_screen()) {
            return;
        }

        if (!base::x11::xcb::extensions::self()->is_randr_available()) {
            return;
        }

        base::x11::xcb::randr::screen_resources res(this->base.x11_data.connection,
                                                    this->base.x11_data.root_window);
        if (res.is_null()) {
            return;
        }

        for (int j = 0; j < res->num_crtcs; ++j) {
            auto crtc = res.crtcs()[j];
            base::x11::xcb::randr::crtc_gamma gamma(this->base.x11_data.connection, crtc);
            if (gamma.is_null()) {
                continue;
            }
            if (gamma->size) {
                qCDebug(KWIN_CORE) << "inverting screen using xcb_randr_set_crtc_gamma";
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
                xcb_randr_set_crtc_gamma(
                    this->base.x11_data.connection, crtc, gamma->size, red, green, blue);
            }
        }
    }

    bool is_sw_compositing() const override
    {
        return !gl_backend;
    }

private:
    static bool has_glx()
    {
        return base::x11::xcb::extensions::self()->has_glx();
    }

    std::unique_ptr<QThread> m_openGLFreezeProtectionThread;
    QTimer* m_openGLFreezeProtection = nullptr;
    Display* m_x11Display;

    std::unique_ptr<glx_backend<type>> gl_backend;
};

}
