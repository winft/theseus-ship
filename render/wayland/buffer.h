/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/buffer.h"
#include "toplevel.h"

#include <QImage>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>
#include <memory>

class QOpenGLFramebufferObject;

namespace KWin::render::wayland
{

struct buffer_win_integration : public render::buffer_win_integration {
public:
    buffer_win_integration(render::buffer const& buffer)
        : render::buffer_win_integration(buffer)
    {
    }

    bool valid() const override
    {
        return external || internal.fbo || !internal.image.isNull();
    }

    QRegion damage() const override
    {
        if (external) {
            if (auto surf = buffer.toplevel()->surface) {
                return surf->trackedDamage();
            }
            return {};
        }
        if (internal.fbo || !internal.image.isNull()) {
            return buffer.toplevel()->damage_region;
        }
        return {};
    }

    std::shared_ptr<Wrapland::Server::Buffer> external;
    struct {
        std::shared_ptr<QOpenGLFramebufferObject> fbo;
        QImage image;
    } internal;
};

}
