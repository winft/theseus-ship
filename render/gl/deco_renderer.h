/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco/renderer.h"

// Must be included before.
#include <epoxy/gl.h>

#include <kwingl/utils.h>

#include <cmath>

namespace KWin::render::gl
{

// Rotates the given source rect 90° counter-clockwise,
// and flips it vertically
inline QImage rotate_and_flip(const QImage& srcImage, const QRect& srcRect)
{
    auto dpr = srcImage.devicePixelRatio();
    QImage image(srcRect.height() * dpr, srcRect.width() * dpr, srcImage.format());
    image.setDevicePixelRatio(dpr);
    const QPoint srcPoint(srcRect.x() * dpr, srcRect.y() * dpr);

    const uint32_t* src = reinterpret_cast<const uint32_t*>(srcImage.bits());
    uint32_t* dst = reinterpret_cast<uint32_t*>(image.bits());

    for (int x = 0; x < image.width(); x++) {
        const uint32_t* s = src + (srcPoint.y() + x) * srcImage.width() + srcPoint.x();
        uint32_t* d = dst + x;

        for (int y = 0; y < image.height(); y++) {
            *d = s[y];
            d += image.width();
        }
    }

    return image;
}

template<typename Scene>
class deco_render_data : public win::deco::render_data
{
public:
    deco_render_data(Scene& scene)
        : scene{scene}
    {
    }

    ~deco_render_data() override
    {
        scene.makeOpenGLContextCurrent();
    }

    std::unique_ptr<GLTexture> texture;

private:
    Scene& scene;
};

template<typename Client, typename Scene>
class deco_renderer : public win::deco::renderer<Client>
{
public:
    enum class DecorationPart : int {
        Left,
        Top,
        Right,
        Bottom,
        Count,
    };

    deco_renderer(Client* client, Scene& scene)
        : win::deco::renderer<Client>(client)
        , scene{scene}
    {
        this->data = std::make_unique<deco_render_data<Scene>>(scene);
        QObject::connect(this->qobject.get(),
                         &win::deco::renderer_qobject::renderScheduled,
                         client->client()->qobject.get(),
                         [win = client->client()](auto const& region) { win->addRepaint(region); });
    }

    void render() override
    {
        auto const scheduled = this->getScheduled();
        const bool dirty = this->areImageSizesDirty();
        if (scheduled.isEmpty() && !dirty) {
            return;
        }
        if (dirty) {
            resizeTexture();
            this->resetImageSizesDirty();
        }

        if (!get_data().texture) {
            // for invalid sizes we get no texture, see BUG 361551
            return;
        }

        QRect left, top, right, bottom;
        auto window = this->client()->client();
        window->layoutDecorationRects(left, top, right, bottom);

        const QRect geometry
            = dirty ? QRect(QPoint(0, 0), window->size()) : scheduled.boundingRect();

        // We pad each part in the decoration atlas in order to avoid texture bleeding.
        const int padding = 1;

        auto renderPart = [=, this](const QRect& geo,
                                    const QRect& partRect,
                                    const QPoint& position,
                                    bool rotated = false) {
            if (!geo.isValid()) {
                return;
            }

            QRect rect = geo;

            // We allow partial decoration updates and it might just so happen that the dirty region
            // is completely contained inside the decoration part, i.e. the dirty region doesn't
            // touch any of the decoration's edges. In that case, we should **not** pad the dirty
            // region.
            if (rect.left() == partRect.left()) {
                rect.setLeft(rect.left() - padding);
            }
            if (rect.top() == partRect.top()) {
                rect.setTop(rect.top() - padding);
            }
            if (rect.right() == partRect.right()) {
                rect.setRight(rect.right() + padding);
            }
            if (rect.bottom() == partRect.bottom()) {
                rect.setBottom(rect.bottom() + padding);
            }

            QRect viewport = geo.translated(-rect.x(), -rect.y());
            auto const devicePixelRatio
                = window->central_output ? window->central_output->scale() : 1.;

            QImage image(rect.size() * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(devicePixelRatio);
            image.fill(Qt::transparent);

            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setViewport(QRect(viewport.topLeft(), viewport.size() * devicePixelRatio));
            painter.setWindow(QRect(geo.topLeft(), geo.size() * devicePixelRatio));
            painter.setClipRect(geo);
            this->renderToPainter(&painter, geo);
            painter.end();

            const QRect viewportScaled(viewport.topLeft() * devicePixelRatio,
                                       viewport.size() * devicePixelRatio);
            const bool isIntegerScaling
                = qFuzzyCompare(devicePixelRatio, std::ceil(devicePixelRatio));
            clamp(image,
                  isIntegerScaling ? viewportScaled : viewportScaled.marginsRemoved({1, 1, 1, 1}));

            if (rotated) {
                // TODO: get this done directly when rendering to the image
                image = rotate_and_flip(image, QRect(QPoint(), rect.size()));
                viewport = QRect(viewport.y(), viewport.x(), viewport.height(), viewport.width());
            }

            const QPoint dirtyOffset = geo.topLeft() - partRect.topLeft();
            get_data().texture->update(
                image, (position + dirtyOffset - viewport.topLeft()) * image.devicePixelRatio());
        };

        const QPoint topPosition(padding, padding);
        const QPoint bottomPosition(padding, topPosition.y() + top.height() + 2 * padding);
        const QPoint leftPosition(padding, bottomPosition.y() + bottom.height() + 2 * padding);
        const QPoint rightPosition(padding, leftPosition.y() + left.width() + 2 * padding);

        renderPart(left.intersected(geometry), left, leftPosition, true);
        renderPart(top.intersected(geometry), top, topPosition);
        renderPart(right.intersected(geometry), right, rightPosition, true);
        renderPart(bottom.intersected(geometry), bottom, bottomPosition);
    }

    std::unique_ptr<win::deco::render_data> reparent() override
    {
        render();
        return this->move_data();
    }

    GLTexture* texture()
    {
        return get_data().texture.get();
    }
    GLTexture* texture() const
    {
        return get_data().texture.get();
    }

private:
    deco_render_data<Scene>& get_data()
    {
        return static_cast<deco_render_data<Scene>&>(*this->data);
    }

    static void clamp_row(int left, int width, int right, const uint32_t* src, uint32_t* dest)
    {
        std::fill_n(dest, left, *src);
        std::copy(src, src + width, dest + left);
        std::fill_n(dest + left + width, right, *(src + width - 1));
    }

    static void clamp_sides(int left, int width, int right, const uint32_t* src, uint32_t* dest)
    {
        std::fill_n(dest, left, *src);
        std::fill_n(dest + left + width, right, *(src + width - 1));
    }

    static void clamp(QImage& image, const QRect& viewport)
    {
        Q_ASSERT(image.depth() == 32);

        const QRect rect = image.rect();

        const int left = viewport.left() - rect.left();
        const int top = viewport.top() - rect.top();
        const int right = rect.right() - viewport.right();
        const int bottom = rect.bottom() - viewport.bottom();

        const int width = rect.width() - left - right;
        const int height = rect.height() - top - bottom;

        const uint32_t* firstRow = reinterpret_cast<uint32_t*>(image.scanLine(top));
        const uint32_t* lastRow = reinterpret_cast<uint32_t*>(image.scanLine(top + height - 1));

        for (int i = 0; i < top; ++i) {
            uint32_t* dest = reinterpret_cast<uint32_t*>(image.scanLine(i));
            clamp_row(left, width, right, firstRow + left, dest);
        }

        for (int i = 0; i < height; ++i) {
            uint32_t* dest = reinterpret_cast<uint32_t*>(image.scanLine(top + i));
            clamp_sides(left, width, right, dest + left, dest);
        }

        for (int i = 0; i < bottom; ++i) {
            uint32_t* dest = reinterpret_cast<uint32_t*>(image.scanLine(top + height + i));
            clamp_row(left, width, right, lastRow + left, dest);
        }
    }

    void resizeTexture()
    {
        auto align = [](int value, int align) { return (value + align - 1) & ~(align - 1); };

        QRect left, top, right, bottom;
        auto window = this->client()->client();
        window->layoutDecorationRects(left, top, right, bottom);
        QSize size;

        size.rwidth()
            = qMax(qMax(top.width(), bottom.width()), qMax(left.height(), right.height()));
        size.rheight() = top.height() + bottom.height() + left.width() + right.width();

        // Reserve some space for padding. We pad decoration parts to avoid texture bleeding.
        const int padding = 1;
        size.rwidth() += 2 * padding;
        size.rheight() += 4 * 2 * padding;

        size.rwidth() = align(size.width(), 128);

        size *= window->central_output ? window->central_output->scale() : 1.;

        auto& data = get_data();

        if (data.texture && data.texture->size() == size) {
            return;
        }

        if (size.isEmpty()) {
            data.texture.reset();
            return;
        }

        data.texture = std::make_unique<GLTexture>(GL_RGBA8, size.width(), size.height());
        data.texture->setYInverted(true);
        data.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        data.texture->clear();
    }

    Scene& scene;
};

}
