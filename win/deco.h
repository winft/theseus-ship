/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "space.h"
#include "structs.h"
#include "wayland/window.h"

#include <KDecoration2/Decoration>
#include <QObject>

namespace KWin::win
{

/**
 * Request showing the application menu bar.
 * @param actionId The DBus menu ID of the action that should be highlighted, 0 for the root menu.
 */
template<typename Win>
void show_application_menu(Win* win, int actionId)
{
    if (auto decoration = win->control->deco().decoration) {
        decoration->showApplicationMenu(actionId);
    } else {
        // No info where application menu button is, show it in the top left corner by default.
        workspace()->showApplicationMenu(QRect(), win, actionId);
    }
}

template<typename Win>
KDecoration2::Decoration* decoration(Win* win)
{
    if (win->control) {
        return win->control->deco().decoration;
    }
    return nullptr;
}

template<typename Win>
bool decoration_has_alpha(Win* win)
{
    return decoration(win) && !decoration(win)->isOpaque();
}

template<typename Win>
void trigger_decoration_repaint(Win* win)
{
    if (auto decoration = win->control->deco().decoration) {
        decoration->update();
    }
}

template<typename Win>
int left_border(Win* win)
{
    if (auto const& deco = decoration(win)) {
        return deco->borderLeft();
    }
    return 0;
}

template<typename Win>
int right_border(Win* win)
{
    if (auto const& deco = decoration(win)) {
        return deco->borderRight();
    }
    return 0;
}

template<typename Win>
int top_border(Win* win)
{
    if (auto const& deco = decoration(win)) {
        return deco->borderTop();
    }
    return 0;
}

template<typename Win>
int bottom_border(Win* win)
{
    if (auto const& deco = decoration(win)) {
        return deco->borderBottom();
    }
    return 0;
}

template<typename Win>
void layout_decoration_rects(Win* win, QRect& left, QRect& top, QRect& right, QRect& bottom)
{
    auto decoration = win->control->deco().decoration;
    if (!decoration) {
        return;
    }
    auto rect = decoration->rect();

    top = QRect(rect.x(), rect.y(), rect.width(), top_border(win));
    bottom = QRect(
        rect.x(), rect.y() + rect.height() - bottom_border(win), rect.width(), bottom_border(win));
    left = QRect(rect.x(),
                 rect.y() + top.height(),
                 left_border(win),
                 rect.height() - top.height() - bottom.height());
    right = QRect(rect.x() + rect.width() - right_border(win),
                  rect.y() + top.height(),
                  right_border(win),
                  rect.height() - top.height() - bottom.height());
}

template<typename Win>
void set_color_scheme(Win* win, QString const& path)
{
    auto scheme = path.isEmpty() ? QStringLiteral("kdeglobals") : path;
    auto& palette = win->control->palette();

    if (palette.current && palette.color_scheme == scheme) {
        // No change.
        return;
    }

    palette.color_scheme = scheme;

    if (palette.current) {
        QObject::disconnect(palette.current.get(), &win::palette::dp::changed, win, nullptr);
    }

    auto it = palette.palettes_registry.find(scheme);

    if (it == palette.palettes_registry.end() || it->expired()) {
        palette.current = std::make_shared<win::palette::dp>(scheme);

        if (palette.current->isValid()) {
            palette.palettes_registry[scheme] = palette.current;
        } else {
            if (!palette.default_palette) {
                palette.default_palette
                    = std::make_shared<win::palette::dp>(QStringLiteral("kdeglobals"));
                palette.palettes_registry[QStringLiteral("kdeglobals")] = palette.default_palette;
            }

            palette.current = palette.default_palette;
        }

        if (scheme == QStringLiteral("kdeglobals")) {
            palette.default_palette = palette.current;
        }

    } else {
        palette.current = it->lock();
    }

    QObject::connect(palette.current.get(), &win::palette::dp::changed, win, [win]() {
        Q_EMIT win->paletteChanged(win->control->palette().q_palette());
    });

    Q_EMIT win->paletteChanged(palette.q_palette());
    Q_EMIT win->colorSchemeChanged();
}

}
