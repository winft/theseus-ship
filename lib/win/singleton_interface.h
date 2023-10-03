/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "kwin_export.h"

// Must be included before QOpenGLFramebufferObject.
#include <epoxy/gl.h>

#include <QImage>
#include <QObject>
#include <QOpenGLFramebufferObject>
#include <QRegion>
#include <QString>
#include <QWindow>
#include <functional>
#include <memory>
#include <string>

class QAction;

namespace KWin::win
{

class subspace;
class subspace_manager_qobject;

struct screen_edger_singleton {
    using callback_t = std::function<bool(electric_border)>;
    std::function<uint32_t(electric_border, callback_t)> reserve;
    std::function<void(electric_border, uint32_t)> unreserve;

    std::function<void(electric_border, QAction*)> reserve_touch;
    std::function<void(electric_border, QAction*)> unreserve_touch;

    std::function<electric_border_action(electric_border)> action_for_touch_border;
};

struct subspaces_singleton {
    win::subspace_manager_qobject* qobject;
    std::function<QVector<subspace*>()> get;
    std::function<subspace*(unsigned int, QString)> create;
    std::function<void(QString const&)> remove;
    std::function<subspace*()> current;
};

class internal_window_singleton : public QObject
{
public:
    using present_fbo_t = std::function<void(std::shared_ptr<QOpenGLFramebufferObject>)>;
    using present_image_t = std::function<void(QImage const& image, QRegion const& damage)>;

    internal_window_singleton(std::function<void()> destroy,
                              present_fbo_t present_fbo,
                              present_image_t present_image)
        : destroy{destroy}
        , present_fbo{present_fbo}
        , present_image{present_image}
    {
    }

    std::function<void()> destroy;
    present_fbo_t present_fbo;
    present_image_t present_image;
};

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static screen_edger_singleton* edger;
    static subspaces_singleton* subspaces;

    static std::function<QRect()> get_current_output_geometry;
    static std::function<std::string(std::string const&)> set_activation_token;
    static std::function<internal_window_singleton*(QWindow*)> create_internal_window;
};

}
