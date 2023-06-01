/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/atom.h"
#include "base/x11/xcb/proto.h"
#include "render/shadow.h"

#include <QVector>

namespace KWin::render::x11
{

template<typename Shadow>
bool update_shadow(Shadow& impl, xcb_connection_t* con, QVector<uint32_t> const& data)
{
    constexpr auto element_count = enum_index(shadow_element::count);

    std::vector<base::x11::xcb::geometry> pixmapGeometries;
    std::vector<xcb_get_image_cookie_t> getImageCookies(element_count);

    for (size_t i = 0; i < element_count; ++i) {
        pixmapGeometries.push_back(base::x11::xcb::geometry(con, data[i]));
    }

    auto discardReplies = [con, &getImageCookies](int start) {
        for (size_t i = start; i < getImageCookies.size(); ++i) {
            xcb_discard_reply(con, getImageCookies.at(i).sequence);
        }
    };

    for (size_t i = 0; i < element_count; ++i) {
        auto& geo = pixmapGeometries[i];
        if (geo.is_null()) {
            discardReplies(0);
            return false;
        }

        getImageCookies[i] = xcb_get_image_unchecked(
            con, XCB_IMAGE_FORMAT_Z_PIXMAP, data[i], 0, 0, geo->width, geo->height, ~0);
    }

    for (size_t i = 0; i < element_count; ++i) {
        auto reply = xcb_get_image_reply(con, getImageCookies.at(i), nullptr);
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
    auto const& id = win.xcb_windows.client;
    if (id == XCB_WINDOW_NONE) {
        return {};
    }

    base::x11::xcb::property property(
        win.space.base.x11_data.connection, false, id, shadow_atom, XCB_ATOM_CARDINAL, 0, 12);
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
bool read_and_update_shadow(Shadow& impl,
                            xcb_connection_t* con,
                            base::x11::xcb::atom const& shadow_atom)
{
    auto data = std::visit(
        overload{[&](auto&& ref_win) { return read_shadow_property(*ref_win, shadow_atom); }},
        *impl.window->ref_win);
    if (data.isEmpty()) {
        return false;
    }
    return update_shadow(impl, con, data);
}

template<typename Shadow, typename Win>
std::unique_ptr<Shadow> create_shadow(Win& win, base::x11::xcb::atom const& shadow_atom)
{
    return std::visit(
        overload{[&](auto&& ref_win) -> std::unique_ptr<Shadow> {
            auto data = read_shadow_property(*ref_win, shadow_atom);
            if (data.isEmpty()) {
                return {};
            }

            auto shadow = ref_win->space.base.render->compositor->scene->createShadow(&win);
            if (!update_shadow(*shadow, ref_win->space.base.x11_data.connection, data)) {
                return {};
            }

            return shadow;
        }},
        *win.ref_win);
}

}
