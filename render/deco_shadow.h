/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "scene.h"
#include "types.h"

#include "win/deco.h"
#include "win/scene.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>

namespace KWin::render
{

template<typename Shadow>
bool update_deco_shadow(Shadow& impl, KDecoration2::Decoration* decoration)
{
    if (impl.m_decorationShadow) {
        // disconnect previous connections
        QObject::disconnect(impl.m_decorationShadow.data(),
                            &KDecoration2::DecorationShadow::innerShadowRectChanged,
                            impl.m_topLevel,
                            nullptr);
        QObject::disconnect(impl.m_decorationShadow.data(),
                            &KDecoration2::DecorationShadow::shadowChanged,
                            impl.m_topLevel,
                            nullptr);
        QObject::disconnect(impl.m_decorationShadow.data(),
                            &KDecoration2::DecorationShadow::paddingChanged,
                            impl.m_topLevel,
                            nullptr);
    }

    impl.m_decorationShadow = decoration->shadow();
    if (!impl.m_decorationShadow) {
        return false;
    }

    // Setup connections - all just mapped to recreate.
    auto update_shadow = [toplevel = impl.m_topLevel]() { win::update_shadow(toplevel); };

    QObject::connect(impl.m_decorationShadow.data(),
                     &KDecoration2::DecorationShadow::innerShadowRectChanged,
                     impl.m_topLevel,
                     update_shadow);
    QObject::connect(impl.m_decorationShadow.data(),
                     &KDecoration2::DecorationShadow::shadowChanged,
                     impl.m_topLevel,
                     update_shadow);
    QObject::connect(impl.m_decorationShadow.data(),
                     &KDecoration2::DecorationShadow::paddingChanged,
                     impl.m_topLevel,
                     update_shadow);

    auto const& p = impl.m_decorationShadow->padding();
    impl.m_topOffset = p.top();
    impl.m_rightOffset = p.right();
    impl.m_bottomOffset = p.bottom();
    impl.m_leftOffset = p.left();

    impl.updateShadowRegion();
    if (!impl.prepareBackend()) {
        return false;
    }
    impl.buildQuads();
    return true;
}

template<typename Shadow, typename Win>
std::unique_ptr<Shadow> create_deco_shadow(Win& win)
{
    if (!win.control) {
        return {};
    }

    auto deco = win::decoration(&win);
    if (!deco) {
        return {};
    }

    auto shadow = win.space.render.scene()->createShadow(&win);
    if (!update_deco_shadow(*shadow, deco)) {
        return {};
    }

    return shadow;
}

}
