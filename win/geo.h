/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_GEO_H
#define KWIN_WIN_GEO_H

#include "types.h"

#include <QRect>

namespace KWin::win
{

/**
 * Calculates the matching client rect for the given frame rect @p rect.
 *
 * Notice that size constraints won't be applied.
 */
template<typename Win>
QRect frame_rect_to_client_rect(Win* win, QRect const& rect)
{
    auto const position = win->framePosToClientPos(rect.topLeft());
    auto const size = win->frameSizeToClientSize(rect.size());
    return QRect(position, size);
}

/**
 * Calculates the matching frame rect for the given client rect @p rect.
 *
 * Notice that size constraints won't be applied.
 */
template<typename Win>
QRect client_rect_to_frame_rect(Win* win, QRect const& rect)
{
    auto const position = win->clientPosToFramePos(rect.topLeft());
    auto const size = win->clientSizeToFrameSize(rect.size());
    return QRect(position, size);
}

}

#endif
