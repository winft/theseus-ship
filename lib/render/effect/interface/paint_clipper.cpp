/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "paint_clipper.h"

#include "effects_handler.h"
#include "types.h"

#include <QStack>

namespace KWin
{

QStack<QRegion>* PaintClipper::areas = nullptr;

PaintClipper::PaintClipper(const QRegion& allowed_area)
    : area(allowed_area)
{
    push(area);
}

PaintClipper::~PaintClipper()
{
    pop(area);
}

void PaintClipper::push(const QRegion& allowed_area)
{
    if (allowed_area == infiniteRegion()) // don't push these
        return;
    if (areas == nullptr)
        areas = new QStack<QRegion>;
    areas->push(allowed_area);
}

void PaintClipper::pop(const QRegion& allowed_area)
{
    if (allowed_area == infiniteRegion())
        return;
    Q_ASSERT(areas != nullptr);
    Q_ASSERT(areas->top() == allowed_area);
    areas->pop();
    if (areas->isEmpty()) {
        delete areas;
        areas = nullptr;
    }
}

bool PaintClipper::clip()
{
    return areas != nullptr;
}

QRegion PaintClipper::paintArea()
{
    Q_ASSERT(areas != nullptr); // can be called only with clip() == true
    const QSize& s = effects->virtualScreenSize();
    QRegion ret(0, 0, s.width(), s.height());
    for (const QRegion& r : qAsConst(*areas)) {
        ret &= r;
    }
    return ret;
}

struct PaintClipper::Iterator::Data {
    Data()
        : index(0)
    {
    }
    int index;
    QRegion region;
};

PaintClipper::Iterator::Iterator()
    : data(new Data)
{
    if (clip() && effects->isOpenGLCompositing()) {
        data->region = paintArea();
        data->index = -1;
        next(); // move to the first one
    }
}

PaintClipper::Iterator::~Iterator()
{
    delete data;
}

bool PaintClipper::Iterator::isDone()
{
    if (!clip())
        return data->index == 1; // run once
    if (effects->isOpenGLCompositing())
        return data->index >= data->region.rectCount(); // run once per each area
    abort();
}

void PaintClipper::Iterator::next()
{
    data->index++;
}

QRect PaintClipper::Iterator::boundingRect() const
{
    if (!clip())
        return infiniteRegion();
    if (effects->isOpenGLCompositing())
        return *(data->region.begin() + data->index);
    abort();
    return infiniteRegion();
}

}
