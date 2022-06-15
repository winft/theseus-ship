/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/proto.h"
#include "render/compositor.h"
#include "render/scene.h"
#include "render/shadow.h"

#include <QVector>

namespace KWin::render::x11
{

template<typename Shadow>
bool update_shadow(Shadow& impl, QVector<uint32_t> const& data)
{
    constexpr auto element_count = enum_index(shadow_element::count);

    QVector<base::x11::xcb::geometry> pixmapGeometries(element_count);
    QVector<xcb_get_image_cookie_t> getImageCookies(element_count);
    auto c = connection();

    for (size_t i = 0; i < element_count; ++i) {
        pixmapGeometries[i] = base::x11::xcb::geometry(data[i]);
    }

    auto discardReplies = [&getImageCookies](int start) {
        for (int i = start; i < getImageCookies.size(); ++i) {
            xcb_discard_reply(connection(), getImageCookies.at(i).sequence);
        }
    };

    for (size_t i = 0; i < element_count; ++i) {
        auto& geo = pixmapGeometries[i];
        if (geo.is_null()) {
            discardReplies(0);
            return false;
        }

        getImageCookies[i] = xcb_get_image_unchecked(
            c, XCB_IMAGE_FORMAT_Z_PIXMAP, data[i], 0, 0, geo->width, geo->height, ~0);
    }

    for (size_t i = 0; i < element_count; ++i) {
        auto reply = xcb_get_image_reply(c, getImageCookies.at(i), nullptr);
        if (!reply) {
            discardReplies(i + 1);
            return false;
        }

        auto& geo = pixmapGeometries[i];
        QImage image(xcb_get_image_data(reply), geo->width, geo->height, QImage::Format_ARGB32);
        impl.m_shadowElements[i] = QPixmap::fromImage(image);

        free(reply);
    }

    impl.m_topOffset = data[element_count];
    impl.m_rightOffset = data[element_count + 1];
    impl.m_bottomOffset = data[element_count + 2];
    impl.m_leftOffset = data[element_count + 3];

    impl.updateShadowRegion();
    if (!impl.prepareBackend()) {
        return false;
    }
    impl.buildQuads();
    return true;
}

template<typename Win>
QVector<uint32_t> read_shadow_property(Win const& win, base::x11::xcb::atom const& shadow_atom)
{
    auto const& id = win.xcb_window;
    if (id == XCB_WINDOW_NONE) {
        return {};
    }

    base::x11::xcb::property property(false, id, shadow_atom, XCB_ATOM_CARDINAL, 0, 12);
    auto shadow = property.value<uint32_t*>();

    if (!shadow) {
        return {};
    }

    QVector<uint32_t> ret;
    ret.reserve(12);

    for (int i = 0; i < 12; ++i) {
        ret << shadow[i];
    }

    return ret;
}

template<typename Shadow>
bool read_and_update_shadow(Shadow& impl, base::x11::xcb::atom const& shadow_atom)
{
    auto data = read_shadow_property(*impl.m_topLevel, shadow_atom);
    if (data.isEmpty()) {
        return false;
    }
    return update_shadow(impl, data);
}

template<typename Shadow, typename Win>
std::unique_ptr<Shadow> create_shadow(Win& win, base::x11::xcb::atom const& shadow_atom)
{
    auto data = read_shadow_property(win, shadow_atom);
    if (data.isEmpty()) {
        return {};
    }

    auto shadow = win.space.render.scene->createShadow(&win);
    if (!update_shadow(*shadow, data)) {
        return {};
    }

    return shadow;
}

}
