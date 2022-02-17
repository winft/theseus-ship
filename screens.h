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

    int current() const;
    void setCurrent(int current);
    /**
     * Called e.g. when a user clicks on a window, set current screen to be the screen
     * where the click occurred
     */
    void setCurrent(const QPoint &pos);
    /**
     * Check whether a client moved completely out of what's considered the current screen,
     * if yes, set a new active screen.
     */
    void setCurrent(Toplevel const* window);
    QRect geometry(int screen) const;
    /**
     * The bounding geometry of all screens combined. Overlapping areas
     * are not counted multiple times.
     * @see geometryChanged()
     */
    QRect geometry() const;
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
     * The highest scale() of all connected screens
     * for use when deciding what scale to load global assets at
     * Similar to QGuiApplication::scale
     * @see scale
     */
    qreal maxScale() const;

    /**
     * The output scale for this display, for use by high DPI displays
     */
    qreal scale(int screen) const;
    /**
     * The bounding size of all screens combined. Overlapping areas
     * are not counted multiple times.
     *
     * @see geometry()
     * @see sizeChanged()
     */
    QSize size() const;

    int intersecting(const QRect &r) const;

    /**
     * The virtual bounding size of all screens combined.
     * The default implementation returns the same as @ref size and that is the
     * method which should be preferred.
     *
     * This method is only for cases where the platform specific implementation needs
     * to support different virtual sizes like on X11 with XRandR panning.
     *
     * @see size
     */
    QSize displaySize() const;


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

    void updateAll();

Q_SIGNALS:
    /**
     * Emitted whenever the screens are changed either count or geometry.
     */
    void changed();
    void currentChanged();
    /**
     * Emitted when the geometry of all screens combined changes.
     * Not emitted when the geometry of an individual screen changes.
     * @see geometry()
     */
    void geometryChanged();
    /**
     * Emitted when the size of all screens combined changes.
     * Not emitted when the size of an individual screen changes.
     * @see size()
     */
    void sizeChanged();
    /**
     * Emitted when the maximum scale of all attached screens changes
     * @see maxScale
     */
    void maxScaleChanged();

private:
    void init();

    void updateSize();
    base::output* findOutput(int screen) const;

    int m_current;
    QSize m_boundingSize;
    qreal m_maxScale;

    base::platform const& base;
};

inline
QSize Screens::size() const
{
    return m_boundingSize;
}

inline
QRect Screens::geometry() const
{
    return QRect(QPoint(0,0), size());
}

}

#endif // KWIN_SCREENS_H
