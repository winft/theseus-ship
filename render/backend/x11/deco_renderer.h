/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"
#include "win/deco/renderer.h"

#include <QTimer>
#include <xcb/xcb.h>

namespace KWin::render::backend::x11
{

class deco_render_data : public win::deco::render_data
{
public:
    deco_render_data(base::x11::data const& x11_data)
        : x11{x11_data}
    {
    }

    ~deco_render_data() override
    {
        if (gc != XCB_NONE) {
            xcb_free_gc(x11.connection, gc);
        }
    }

    xcb_gcontext_t gc{XCB_NONE};
    base::x11::data const& x11;
};

class deco_renderer : public win::deco::render_injector
{
public:
    explicit deco_renderer(base::x11::data const& x11_data, win::deco::render_window window)
        : win::deco::render_injector(std::move(window))
        , m_scheduleTimer(new QTimer(this->qobject.get()))
    {
        this->data = std::make_unique<deco_render_data>(x11_data);

        // delay any rendering to end of event cycle to catch multiple updates per cycle
        m_scheduleTimer->setSingleShot(true);
        m_scheduleTimer->setInterval(0);
        QObject::connect(
            m_scheduleTimer, &QTimer::timeout, this->qobject.get(), [this] { render(); });
        QObject::connect(this->qobject.get(),
                         &win::deco::renderer_qobject::renderScheduled,
                         m_scheduleTimer,
                         static_cast<void (QTimer::*)()>(&QTimer::start));
    }

protected:
    void render() override
    {
        auto const scheduled = this->getScheduled();
        if (scheduled.isEmpty()) {
            return;
        }

        auto& data = static_cast<deco_render_data&>(*this->data);
        auto c = data.x11.connection;

        if (data.gc == XCB_NONE) {
            data.gc = xcb_generate_id(c);
            xcb_create_gc(c, data.gc, this->window.frame_id, 0, nullptr);
        }

        QRect left, top, right, bottom;
        this->window.layout_rects(left, top, right, bottom);

        const QRect geometry = scheduled.boundingRect();
        left = left.intersected(geometry);
        top = top.intersected(geometry);
        right = right.intersected(geometry);
        bottom = bottom.intersected(geometry);

        auto renderPart = [this, c, &data](const QRect& geo) {
            if (!geo.isValid()) {
                return;
            }

            auto image = this->renderToImage(geo);
            xcb_put_image(c,
                          XCB_IMAGE_FORMAT_Z_PIXMAP,
                          this->window.frame_id,
                          data.gc,
                          image.width(),
                          image.height(),
                          geo.x(),
                          geo.y(),
                          0,
                          this->window.bit_depth(),
                          image.sizeInBytes(),
                          image.constBits());
        };

        renderPart(left);
        renderPart(top);
        renderPart(right);
        renderPart(bottom);

        xcb_flush(c);
        this->image_size_dirty = false;
    }

private:
    QTimer* m_scheduleTimer;
};

}
