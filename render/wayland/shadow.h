/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/types.h"
#include "utils/algorithm.h"

#include <QPixmap>
#include <QPointer>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

template<typename Shadow>
bool update_shadow(Shadow& impl)
{
    auto surface = impl.window->ref_win->surface;
    if (!surface) {
        return false;
    }

    auto const& shadow = surface->state().shadow;
    if (!shadow) {
        return false;
    }

    impl.m_shadowElements[enum_index(shadow_element::top)] = shadow->top()
        ? QPixmap::fromImage(shadow->top()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::top_right)] = shadow->topRight()
        ? QPixmap::fromImage(shadow->topRight()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::right)] = shadow->right()
        ? QPixmap::fromImage(shadow->right()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::bottom_right)] = shadow->bottomRight()
        ? QPixmap::fromImage(shadow->bottomRight()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::bottom)] = shadow->bottom()
        ? QPixmap::fromImage(shadow->bottom()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::bottom_left)] = shadow->bottomLeft()
        ? QPixmap::fromImage(shadow->bottomLeft()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::left)] = shadow->left()
        ? QPixmap::fromImage(shadow->left()->shmImage()->createQImage().copy())
        : QPixmap();
    impl.m_shadowElements[enum_index(shadow_element::top_left)] = shadow->topLeft()
        ? QPixmap::fromImage(shadow->topLeft()->shmImage()->createQImage().copy())
        : QPixmap();

    auto const& p = shadow->offset();
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
std::unique_ptr<Shadow> create_shadow(Win& win)
{
    auto surface = win.ref_win->surface;
    if (!surface || !surface->state().shadow) {
        return {};
    }

    auto shadow = win.ref_win->space.base.render->compositor->scene->createShadow(&win);
    if (!update_shadow(*shadow)) {
        return {};
    }

    return shadow;
}

}
