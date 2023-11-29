/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

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
    if (!std::visit(overload{[&](auto&& ref_win) {
                        if (impl.m_decorationShadow) {
                            // disconnect previous connections
                            QObject::disconnect(
                                impl.m_decorationShadow.get(),
                                &KDecoration2::DecorationShadow::innerShadowRectChanged,
                                ref_win->qobject.get(),
                                nullptr);
                            QObject::disconnect(impl.m_decorationShadow.get(),
                                                &KDecoration2::DecorationShadow::shadowChanged,
                                                ref_win->qobject.get(),
                                                nullptr);
                            QObject::disconnect(impl.m_decorationShadow.get(),
                                                &KDecoration2::DecorationShadow::paddingChanged,
                                                ref_win->qobject.get(),
                                                nullptr);
                        }

                        impl.m_decorationShadow = decoration->shadow();
                        if (!impl.m_decorationShadow) {
                            return false;
                        }

                        // Setup connections - all just mapped to recreate.
                        auto update_shadow = [ref_win]() { win::update_shadow(ref_win); };

                        QObject::connect(impl.m_decorationShadow.get(),
                                         &KDecoration2::DecorationShadow::innerShadowRectChanged,
                                         ref_win->qobject.get(),
                                         update_shadow);
                        QObject::connect(impl.m_decorationShadow.get(),
                                         &KDecoration2::DecorationShadow::shadowChanged,
                                         ref_win->qobject.get(),
                                         update_shadow);
                        QObject::connect(impl.m_decorationShadow.get(),
                                         &KDecoration2::DecorationShadow::paddingChanged,
                                         ref_win->qobject.get(),
                                         update_shadow);
                        return true;
                    }},
                    *impl.window->ref_win)) {
        return false;
    }

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
    return std::visit(overload{[&](auto&& ref_win) -> std::unique_ptr<Shadow> {
                          if (!ref_win->control) {
                              return {};
                          }

                          auto deco = win::decoration(ref_win);
                          if (!deco) {
                              return {};
                          }

                          auto shadow = ref_win->space.base.mod.render->scene->createShadow(&win);
                          if (!update_deco_shadow(*shadow, deco)) {
                              return {};
                          }

                          return shadow;
                      }},
                      *win.ref_win);
}

}
