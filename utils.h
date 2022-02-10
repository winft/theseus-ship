/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#ifndef KWIN_UTILS_H
#define KWIN_UTILS_H

// cmake stuff
#include <config-kwin.h>
#include <kwinconfig.h>
// kwin
#include <kwinglobals.h>
#include "win/types.h"

#include <QLoggingCategory>
#include <QList>
#include <QPoint>
#include <QRect>
#include <climits>

KWIN_EXPORT Q_DECLARE_LOGGING_CATEGORY(KWIN_CORE)
Q_DECLARE_LOGGING_CATEGORY(KWIN_PERF)
namespace KWin
{

const QPoint invalidPoint(INT_MIN, INT_MIN);

enum StrutArea {
    StrutAreaInvalid = 0, // Null
    StrutAreaTop     = 1 << 0,
    StrutAreaRight   = 1 << 1,
    StrutAreaBottom  = 1 << 2,
    StrutAreaLeft    = 1 << 3,
    StrutAreaAll     = StrutAreaTop | StrutAreaRight | StrutAreaBottom | StrutAreaLeft
};
Q_DECLARE_FLAGS(StrutAreas, StrutArea)

class KWIN_EXPORT StrutRect : public QRect
{
public:
    explicit StrutRect(QRect rect = QRect(), StrutArea area = StrutAreaInvalid);
    StrutRect(const StrutRect& other);
    StrutRect &operator=(const StrutRect& other);
    inline StrutArea area() const {
        return m_area;
    }
private:
    StrutArea m_area;
};
typedef QVector<StrutRect> StrutRects;

void KWIN_EXPORT updateXTime();
void KWIN_EXPORT grabXServer();
void KWIN_EXPORT ungrabXServer();
bool KWIN_EXPORT grabXKeyboard(xcb_window_t w = XCB_WINDOW_NONE);
void KWIN_EXPORT ungrabXKeyboard();

/**
 * Small helper class which performs grabXServer in the ctor and
 * ungrabXServer in the dtor. Use this class to ensure that grab and
 * ungrab are matched.
 */
class XServerGrabber
{
public:
    XServerGrabber() {
        grabXServer();
    }
    ~XServerGrabber() {
        ungrabXServer();
    }
};

// converting between X11 mouse/keyboard state mask and Qt button/keyboard states
Qt::MouseButton x11ToQtMouseButton(int button);
Qt::MouseButton KWIN_EXPORT x11ToQtMouseButton(int button);
Qt::MouseButtons KWIN_EXPORT x11ToQtMouseButtons(int state);
Qt::KeyboardModifiers KWIN_EXPORT x11ToQtKeyboardModifiers(int state);

} // namespace

// Must be outside namespace
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::StrutAreas)

#endif
