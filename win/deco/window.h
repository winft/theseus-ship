/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>

namespace KWin::win::deco
{

/**
 * Wrapper class for windows.
 */
template<typename RefWin>
class window : public QObject
{
public:
    RefWin* win;

    explicit window(RefWin* win)
        : win(win)
    {
    }
};

}
