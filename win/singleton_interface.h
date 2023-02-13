/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "kwinglobals.h"

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

class virtual_desktop;
class virtual_desktop_manager_qobject;

struct screen_edger_singleton {
    using callback_t = std::function<bool(ElectricBorder)>;
    std::function<uint32_t(ElectricBorder, callback_t)> reserve;
    std::function<void(ElectricBorder, uint32_t)> unreserve;

    std::function<void(ElectricBorder, QAction*)> reserve_touch;
    std::function<void(ElectricBorder, QAction*)> unreserve_touch;

    std::function<ElectricBorderAction(ElectricBorder)> action_for_touch_border;
};

struct virtual_desktops_singleton {
    win::virtual_desktop_manager_qobject* qobject;
    std::function<QVector<virtual_desktop*>()> get;
    std::function<void(unsigned int, QString)> create;
    std::function<void(QString const&)> remove;
    std::function<virtual_desktop*()> current;
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
    static virtual_desktops_singleton* virtual_desktops;

    static std::function<QRect()> get_current_output_geometry;
    static std::function<std::string(std::string const&)> set_activation_token;
    static std::function<internal_window_singleton*(QWindow*)> create_internal_window;
};

}
