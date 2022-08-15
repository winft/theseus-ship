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

template<typename Client>
class deco_renderer : public win::deco::renderer<Client>
{
public:
    explicit deco_renderer(Client* client)
        : win::deco::renderer<Client>(client)
        , m_scheduleTimer(new QTimer(this->qobject.get()))
        , m_gc(XCB_NONE)
    {
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

    ~deco_renderer() override
    {
        if (m_gc != XCB_NONE) {
            xcb_free_gc(connection(), m_gc);
        }
    }

    void reparent() override
    {
        if (m_scheduleTimer->isActive()) {
            m_scheduleTimer->stop();
        }
        QObject::disconnect(m_scheduleTimer, &QTimer::timeout, this->qobject.get(), nullptr);
        QObject::disconnect(this->qobject.get(),
                            &win::deco::renderer_qobject::renderScheduled,
                            m_scheduleTimer,
                            static_cast<void (QTimer::*)()>(&QTimer::start));
        win::deco::renderer<Client>::reparent();
    }

protected:
    void render() override
    {
        if (!this->client()) {
            return;
        }
        auto const scheduled = this->getScheduled();
        if (scheduled.isEmpty()) {
            return;
        }

        auto c = connection();
        auto window = this->client()->client();

        if (m_gc == XCB_NONE) {
            m_gc = xcb_generate_id(c);
            xcb_create_gc(c, m_gc, window->frameId(), 0, nullptr);
        }

        QRect left, top, right, bottom;
        window->layoutDecorationRects(left, top, right, bottom);

        const QRect geometry = scheduled.boundingRect();
        left = left.intersected(geometry);
        top = top.intersected(geometry);
        right = right.intersected(geometry);
        bottom = bottom.intersected(geometry);

        auto renderPart = [this, c](const QRect& geo) {
            if (!geo.isValid()) {
                return;
            }

            auto image = this->renderToImage(geo);
            auto window = this->client()->client();

            xcb_put_image(c,
                          XCB_IMAGE_FORMAT_Z_PIXMAP,
                          window->frameId(),
                          m_gc,
                          image.width(),
                          image.height(),
                          geo.x(),
                          geo.y(),
                          0,
                          window->bit_depth,
                          image.sizeInBytes(),
                          image.constBits());
        };

        renderPart(left);
        renderPart(top);
        renderPart(right);
        renderPart(bottom);

        xcb_flush(c);
        this->resetImageSizesDirty();
    }

private:
    QTimer* m_scheduleTimer;
    xcb_gcontext_t m_gc;
};

}
