/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects_export.h>

#include <QRegion>

namespace KWin
{

/**
 * @short Helper class for restricting painting area only to allowed area.
 *
 * This helper class helps specifying areas that should be painted, clipping
 * out the rest. The simplest usage is creating an object on the stack
 * and giving it the area that is allowed to be painted to. When the object
 * is destroyed, the restriction will be removed.
 * Note that all painting code must use paintArea() to actually perform the clipping.
 */
class KWINEFFECTS_EXPORT PaintClipper
{
public:
    /**
     * Calls push().
     */
    explicit PaintClipper(const QRegion& allowed_area);
    /**
     * Calls pop().
     */
    ~PaintClipper();
    /**
     * Allows painting only in the given area. When areas have been already
     * specified, painting is allowed only in the intersection of all areas.
     */
    static void push(const QRegion& allowed_area);
    /**
     * Removes the given area. It must match the top item in the stack.
     */
    static void pop(const QRegion& allowed_area);
    /**
     * Returns true if any clipping should be performed.
     */
    static bool clip();
    /**
     * If clip() returns true, this function gives the resulting area in which
     * painting is allowed. It is usually simpler to use the helper Iterator class.
     */
    static QRegion paintArea();
    /**
     * Helper class to perform the clipped painting. The usage is:
     * @code
     * for ( PaintClipper::Iterator iterator;
     *      !iterator.isDone();
     *      iterator.next())
     *     { // do the painting, possibly use iterator.boundingRect()
     *     }
     * @endcode
     */
    class KWINEFFECTS_EXPORT Iterator
    {
    public:
        Iterator();
        ~Iterator();
        bool isDone();
        void next();
        QRect boundingRect() const;

    private:
        struct Data;
        Data* data;
    };

private:
    QRegion area;
    static QStack<QRegion>* areas;
};

}
