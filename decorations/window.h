/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>

namespace KWin
{
class Toplevel;

namespace Decoration
{

/**
 * Wrapper class for windows.
 */
class window : public QObject
{
    Q_OBJECT
public:
    Toplevel* win;

    explicit window(Toplevel* win);
};

}
}
