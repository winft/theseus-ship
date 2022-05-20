/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <kwin_export.h>

namespace KWin
{

class Toplevel;

namespace win::deco
{

/**
 * Wrapper class for windows.
 */
class KWIN_EXPORT window : public QObject
{
    Q_OBJECT
public:
    Toplevel* win;

    explicit window(Toplevel* win);
};

}
}
