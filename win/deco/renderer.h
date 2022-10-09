/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "decorations_logging.h"

#include "kwin_export.h"
#include "win/damage.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <QObject>
#include <QPainter>
#include <QRegion>
#include <functional>
#include <memory>

namespace KWin::win::deco
{

class render_data
{
public:
    virtual ~render_data() = default;
};

class KWIN_EXPORT renderer_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void renderScheduled(QRegion const& geo);
};

struct render_window {
    std::function<QRect()> geo;
    std::function<double()> scale;
    std::function<int()> bit_depth;
    std::function<void(QRect&, QRect&, QRect&, QRect&)> layout_rects;

    KDecoration2::Decoration* deco;
    xcb_window_t frame_id{XCB_WINDOW_NONE};
};

class render_injector
{
public:
    using qobject_t = renderer_qobject;

    render_injector(render_window window)
        : qobject{std::make_unique<renderer_qobject>()}
        , window{std::move(window)}
    {
    }

    virtual ~render_injector() = default;

    virtual void render() = 0;

    std::unique_ptr<renderer_qobject> qobject;
    std::unique_ptr<render_data> data;

    QRegion scheduled;
    bool image_size_dirty{true};

protected:
    QRegion getScheduled()
    {
        auto const region = scheduled;
        scheduled = {};
        return region;
    }

    QImage renderToImage(const QRect& geo)
    {
        auto dpr = window.scale();

        // Guess the pixel format of the X pixmap into which the QImage will be copied.
        QImage::Format format;
        const int depth = window.bit_depth();
        switch (depth) {
        case 30:
            format = QImage::Format_A2RGB30_Premultiplied;
            break;
        case 24:
        case 32:
            format = QImage::Format_ARGB32_Premultiplied;
            break;
        default:
            qCCritical(KWIN_DECORATIONS) << "Unsupported client depth" << depth;
            format = QImage::Format_ARGB32_Premultiplied;
            break;
        };

        QImage image(geo.width() * dpr, geo.height() * dpr, format);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setWindow(QRect(geo.topLeft(), geo.size() * dpr));
        p.setClipRect(geo);
        renderToPainter(&p, geo);
        return image;
    }

    void renderToPainter(QPainter* painter, const QRect& rect)
    {
        window.deco->paint(painter, rect);
    }

    render_window window;
};

template<typename Client>
class renderer
{
public:
    explicit renderer(Client* client)
        : m_client(client)
    {
        auto injector_window
            = render_window{[client] { return client->client()->geo.frame; },
                            [client] {
                                if (auto out = client->client()->topo.central_output) {
                                    return out->scale();
                                }
                                return 1.;
                            },
                            [client] { return client->client()->render_data.bit_depth; },
                            [client](auto& left, auto& top, auto& right, auto& bottom) {
                                client->client()->layoutDecorationRects(left, top, right, bottom);
                            },
                            client->decoration(),
                            client->client()->frameId()};

        auto render = client->client()->space.base.render.get();
        if (auto& scene = render->compositor->scene) {
            injector = scene->create_deco(std::move(injector_window));
        } else {
            if constexpr (requires(decltype(render) render, render_window window) {
                              render->create_non_composited_deco(window);
                          }) {
                injector = render->create_non_composited_deco(std::move(injector_window));
            }
        }
        assert(injector);

        auto markImageSizesDirty = [this] { injector->image_size_dirty = true; };
        QObject::connect(client->decoration(),
                         &KDecoration2::Decoration::damaged,
                         injector->qobject.get(),
                         [this](auto const& rect) {
                             if (!m_client) {
                                 return;
                             }
                             injector->scheduled = injector->scheduled.united(rect);
                             win::add_repaint(*m_client->client(), rect);
                             Q_EMIT injector->qobject->renderScheduled(rect);
                         });
        QObject::connect(client->client()->qobject.get(),
                         &decltype(client->client()->qobject)::element_type::central_output_changed,
                         injector->qobject.get(),
                         [markImageSizesDirty](auto old_out, auto new_out) {
                             if (!new_out) {
                                 return;
                             }
                             if (old_out && old_out->scale() == new_out->scale()) {
                                 return;
                             }
                             markImageSizesDirty();
                         });
        QObject::connect(client->decoration(),
                         &KDecoration2::Decoration::bordersChanged,
                         injector->qobject.get(),
                         markImageSizesDirty);
        QObject::connect(client->decoratedClient(),
                         &KDecoration2::DecoratedClient::widthChanged,
                         injector->qobject.get(),
                         markImageSizesDirty);
        QObject::connect(client->decoratedClient(),
                         &KDecoration2::DecoratedClient::heightChanged,
                         injector->qobject.get(),
                         markImageSizesDirty);
    }

    std::unique_ptr<win::deco::render_data> move_data()
    {
        injector->render();
        m_client = nullptr;
        injector->qobject.reset();
        return std::move(injector->data);
    }

    Client* m_client;
    std::unique_ptr<render_injector> injector;
};

}
