/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_SCREENS_H
#define KWIN_SCREENS_H

// KWin includes
#include <kwinglobals.h>

#include <QObject>
#include <QRect>
#include <QVector>

namespace KWin
{
namespace base
{
class output;
class platform;
}
class Toplevel;

class KWIN_EXPORT Screens : public QObject
{
    Q_OBJECT
public:
    Screens(base::platform const& base);
    ~Screens() override;

    QRect geometry(int screen) const;
    /**
     * The output name of the screen (usually eg. LVDS-1, VGA-0 or DVI-I-1 etc.)
     */
    QString name(int screen) const;
    /**
     * @returns current refreshrate of the @p screen.
     */
    float refreshRate(int screen) const;
    /**
     * @returns size of the @p screen.
     *
     * To get the size of all screens combined use size().
     * @see size()
     */
    QSize size(int screen) const;

    /**
     * The output scale for this display, for use by high DPI displays
     */
    qreal scale(int screen) const;

    int intersecting(const QRect &r) const;


    /**
     * The physical size of @p screen in mm.
     * Default implementation returns a size derived from 96 DPI.
     */
    QSizeF physicalSize(int screen) const;

    /**
     * @returns @c true if the @p screen is connected through an internal display (e.g. LVDS).
     * Default implementation returns @c false.
     */
    bool isInternal(int screen) const;

    Qt::ScreenOrientation orientation(int screen) const;

    int physicalDpiX(int screen) const;
    int physicalDpiY(int screen) const;

private:
    base::output* findOutput(int screen) const;

    base::platform const& base;
};

}

#endif // KWIN_SCREENS_H
