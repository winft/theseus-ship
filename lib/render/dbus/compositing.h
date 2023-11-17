/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include "render/compositor_qobject.h"
#include "render/types.h"

#include <QObject>
#include <QtDBus>
#include <functional>

#include <epoxy/gl.h>
// Must be included before.
#include <QOpenGLContext>

namespace KWin::render::dbus
{

struct compositing_integration {
    std::function<bool(void)> active;
    std::function<bool(void)> required;
    std::function<bool(void)> possible;
    std::function<QString(void)> not_possible_reason;
    std::function<bool(void)> opengl_broken;
    std::function<QString(void)> type;

    std::function<QStringList(void)> get_types;
    std::function<void(void)> reinit;
};

class KWIN_EXPORT compositing_qobject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Compositing")

    /**
     * @brief Whether the Compositor is active. That is a Scene is present and the Compositor is
     * not shutting down itself.
     */
    Q_PROPERTY(bool active READ isActive)
    /**
     * @brief Whether compositing is possible. Mostly means whether the required X extensions
     * are available.
     */
    Q_PROPERTY(bool compositingPossible READ isCompositingPossible)
    /**
     * @brief The reason why compositing is not possible. Empty String if compositing is possible.
     */
    Q_PROPERTY(QString compositingNotPossibleReason READ compositingNotPossibleReason)
    /**
     * @brief Whether OpenGL has failed badly in the past (crash) and is considered as broken.
     */
    Q_PROPERTY(bool openGLIsBroken READ isOpenGLBroken)
    /**
     * The type of the currently used Scene:
     * @li @c none No Compositing
     * @li @c gl1 OpenGL 1
     * @li @c gl2 OpenGL 2
     * @li @c gles OpenGL ES 2
     */
    Q_PROPERTY(QString compositingType READ compositingType)
    /**
     * @brief All currently supported OpenGLPlatformInterfaces.
     *
     * Possible values:
     * @li glx
     * @li egl
     *
     * Values depend on operation mode and compile time options.
     */
    Q_PROPERTY(QStringList supportedOpenGLPlatformInterfaces READ supportedOpenGLPlatformInterfaces)
    Q_PROPERTY(bool platformRequiresCompositing READ platformRequiresCompositing)

public:
    compositing_qobject();
    ~compositing_qobject() = default;

    bool isActive() const;
    bool isCompositingPossible() const;
    QString compositingNotPossibleReason() const;
    bool isOpenGLBroken() const;
    QString compositingType() const;
    QStringList supportedOpenGLPlatformInterfaces() const;
    bool platformRequiresCompositing() const;

    compositing_integration integration;

public Q_SLOTS:
    /**
     * @brief Used by Compositing KCM after settings change.
     *
     * On signal Compositor reloads settings and restarts.
     */
    void reinitialize();

Q_SIGNALS:
    void compositingToggled(bool active);
};

template<typename Compositor>
class compositing
{
public:
    explicit compositing(Compositor& comp)
        : qobject{std::make_unique<compositing_qobject>()}
        , compositor{comp}
    {
        qobject->integration.active = [this] { return compositor.state == state::on; };
        qobject->integration.required = [this] { return compositor.requiresCompositing(); };
        qobject->integration.possible = [this] { return compositor.compositingPossible(); };
        qobject->integration.not_possible_reason
            = [this] { return compositor.compositingNotPossibleReason(); };
        qobject->integration.opengl_broken
            = [this] { return compositor.openGLCompositingIsBroken(); };
        qobject->integration.type = [this] {
            if (!compositor.scene) {
                return QStringLiteral("none");
            }

            if (compositor.scene->isOpenGl()) {
                if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
                    return QStringLiteral("gles");
                } else {
                    return QStringLiteral("gl2");
                }
            } else {
                return QStringLiteral("qpainter");
            }
        };
        qobject->integration.reinit = [this] { return compositor.reinitialize(); };

        QObject::connect(compositor.qobject.get(),
                         &render::compositor_qobject::compositingToggled,
                         qobject.get(),
                         &compositing_qobject::compositingToggled);
    }

    std::unique_ptr<compositing_qobject> qobject;

private:
    Compositor& compositor;
};
}
