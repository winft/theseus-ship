/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "paint_data.h"

#include "effect_window.h"

#include <QMatrix4x4>

namespace KWin
{

class PaintDataPrivate
{
public:
    PaintDataPrivate()
        : scale(1., 1., 1.)
        , rotationAxis(0, 0, 1.)
        , rotationAngle(0.)
    {
    }
    QVector3D scale;
    QVector3D translation;

    QVector3D rotationAxis;
    QVector3D rotationOrigin;
    qreal rotationAngle;
};

PaintData::PaintData()
    : d(new PaintDataPrivate())
{
}

PaintData::~PaintData()
{
    delete d;
}

qreal PaintData::xScale() const
{
    return d->scale.x();
}

qreal PaintData::yScale() const
{
    return d->scale.y();
}

qreal PaintData::zScale() const
{
    return d->scale.z();
}

void PaintData::setScale(const QVector2D& scale)
{
    d->scale.setX(scale.x());
    d->scale.setY(scale.y());
}

void PaintData::setScale(const QVector3D& scale)
{
    d->scale = scale;
}
void PaintData::setXScale(qreal scale)
{
    d->scale.setX(scale);
}

void PaintData::setYScale(qreal scale)
{
    d->scale.setY(scale);
}

void PaintData::setZScale(qreal scale)
{
    d->scale.setZ(scale);
}

const QVector3D& PaintData::scale() const
{
    return d->scale;
}

void PaintData::setXTranslation(qreal translate)
{
    d->translation.setX(translate);
}

void PaintData::setYTranslation(qreal translate)
{
    d->translation.setY(translate);
}

void PaintData::setZTranslation(qreal translate)
{
    d->translation.setZ(translate);
}

void PaintData::translate(qreal x, qreal y, qreal z)
{
    translate(QVector3D(x, y, z));
}

void PaintData::translate(const QVector3D& t)
{
    d->translation += t;
}

qreal PaintData::xTranslation() const
{
    return d->translation.x();
}

qreal PaintData::yTranslation() const
{
    return d->translation.y();
}

qreal PaintData::zTranslation() const
{
    return d->translation.z();
}

const QVector3D& PaintData::translation() const
{
    return d->translation;
}

qreal PaintData::rotationAngle() const
{
    return d->rotationAngle;
}

QVector3D PaintData::rotationAxis() const
{
    return d->rotationAxis;
}

QVector3D PaintData::rotationOrigin() const
{
    return d->rotationOrigin;
}

void PaintData::setRotationAngle(qreal angle)
{
    d->rotationAngle = angle;
}

void PaintData::setRotationAxis(Qt::Axis axis)
{
    switch (axis) {
    case Qt::XAxis:
        setRotationAxis(QVector3D(1, 0, 0));
        break;
    case Qt::YAxis:
        setRotationAxis(QVector3D(0, 1, 0));
        break;
    case Qt::ZAxis:
        setRotationAxis(QVector3D(0, 0, 1));
        break;
    }
}

void PaintData::setRotationAxis(const QVector3D& axis)
{
    d->rotationAxis = axis;
}

void PaintData::setRotationOrigin(const QVector3D& origin)
{
    d->rotationOrigin = origin;
}

class WindowPaintDataPrivate
{
public:
    qreal opacity;
    qreal saturation;
    qreal brightness;
    int screen;
    qreal crossFadeProgress;
    QMatrix4x4 pMatrix;
    QMatrix4x4 mvMatrix;
    QMatrix4x4 screenProjectionMatrix;
};

WindowPaintData::WindowPaintData(EffectWindow* w)
    : WindowPaintData(w, QMatrix4x4())
{
}

WindowPaintData::WindowPaintData(EffectWindow* w, const QMatrix4x4& screenProjectionMatrix)
    : PaintData()
    , shader(nullptr)
    , d(new WindowPaintDataPrivate())
{
    d->screenProjectionMatrix = screenProjectionMatrix;
    quads = w->buildQuads();
    setOpacity(w->opacity());
    setSaturation(1.0);
    setBrightness(1.0);
    setScreen(0);
    setCrossFadeProgress(1.0);
}

WindowPaintData::WindowPaintData(const WindowPaintData& other)
    : PaintData()
    , quads(other.quads)
    , shader(other.shader)
    , d(new WindowPaintDataPrivate())
{
    setXScale(other.xScale());
    setYScale(other.yScale());
    setZScale(other.zScale());
    translate(other.translation());
    setRotationOrigin(other.rotationOrigin());
    setRotationAxis(other.rotationAxis());
    setRotationAngle(other.rotationAngle());
    setOpacity(other.opacity());
    setSaturation(other.saturation());
    setBrightness(other.brightness());
    setScreen(other.screen());
    setCrossFadeProgress(other.crossFadeProgress());
    setProjectionMatrix(other.projectionMatrix());
    setModelViewMatrix(other.modelViewMatrix());
    d->screenProjectionMatrix = other.d->screenProjectionMatrix;
}

WindowPaintData::~WindowPaintData()
{
    delete d;
}

qreal WindowPaintData::opacity() const
{
    return d->opacity;
}

qreal WindowPaintData::saturation() const
{
    return d->saturation;
}

qreal WindowPaintData::brightness() const
{
    return d->brightness;
}

int WindowPaintData::screen() const
{
    return d->screen;
}

void WindowPaintData::setOpacity(qreal opacity)
{
    d->opacity = opacity;
}

void WindowPaintData::setSaturation(qreal saturation) const
{
    d->saturation = saturation;
}

void WindowPaintData::setBrightness(qreal brightness)
{
    d->brightness = brightness;
}

void WindowPaintData::setScreen(int screen) const
{
    d->screen = screen;
}

qreal WindowPaintData::crossFadeProgress() const
{
    return d->crossFadeProgress;
}

void WindowPaintData::setCrossFadeProgress(qreal factor)
{
    d->crossFadeProgress = qBound(qreal(0.0), factor, qreal(1.0));
}

qreal WindowPaintData::multiplyOpacity(qreal factor)
{
    d->opacity *= factor;
    return d->opacity;
}

qreal WindowPaintData::multiplySaturation(qreal factor)
{
    d->saturation *= factor;
    return d->saturation;
}

qreal WindowPaintData::multiplyBrightness(qreal factor)
{
    d->brightness *= factor;
    return d->brightness;
}

void WindowPaintData::setProjectionMatrix(const QMatrix4x4& matrix)
{
    d->pMatrix = matrix;
}

QMatrix4x4 WindowPaintData::projectionMatrix() const
{
    return d->pMatrix;
}

QMatrix4x4& WindowPaintData::rprojectionMatrix()
{
    return d->pMatrix;
}

void WindowPaintData::setModelViewMatrix(const QMatrix4x4& matrix)
{
    d->mvMatrix = matrix;
}

QMatrix4x4 WindowPaintData::modelViewMatrix() const
{
    return d->mvMatrix;
}

QMatrix4x4& WindowPaintData::rmodelViewMatrix()
{
    return d->mvMatrix;
}

WindowPaintData& WindowPaintData::operator*=(qreal scale)
{
    this->setXScale(this->xScale() * scale);
    this->setYScale(this->yScale() * scale);
    this->setZScale(this->zScale() * scale);
    return *this;
}

WindowPaintData& WindowPaintData::operator*=(const QVector2D& scale)
{
    this->setXScale(this->xScale() * scale.x());
    this->setYScale(this->yScale() * scale.y());
    return *this;
}

WindowPaintData& WindowPaintData::operator*=(const QVector3D& scale)
{
    this->setXScale(this->xScale() * scale.x());
    this->setYScale(this->yScale() * scale.y());
    this->setZScale(this->zScale() * scale.z());
    return *this;
}

WindowPaintData& WindowPaintData::operator+=(const QPointF& translation)
{
    return this->operator+=(QVector3D(translation));
}

WindowPaintData& WindowPaintData::operator+=(const QPoint& translation)
{
    return this->operator+=(QVector3D(translation));
}

WindowPaintData& WindowPaintData::operator+=(const QVector2D& translation)
{
    return this->operator+=(QVector3D(translation));
}

WindowPaintData& WindowPaintData::operator+=(const QVector3D& translation)
{
    translate(translation);
    return *this;
}

QMatrix4x4 WindowPaintData::screenProjectionMatrix() const
{
    return d->screenProjectionMatrix;
}

class ScreenPaintData::Private
{
public:
    QMatrix4x4 projectionMatrix;
    EffectScreen* screen = nullptr;
};

ScreenPaintData::ScreenPaintData()
    : PaintData()
    , d(new Private())
{
}

ScreenPaintData::ScreenPaintData(const QMatrix4x4& projectionMatrix, EffectScreen* screen)
    : PaintData()
    , d(new Private())
{
    d->projectionMatrix = projectionMatrix;
    d->screen = screen;
}

ScreenPaintData::~ScreenPaintData() = default;

ScreenPaintData::ScreenPaintData(const ScreenPaintData& other)
    : PaintData()
    , d(new Private())
{
    translate(other.translation());
    setXScale(other.xScale());
    setYScale(other.yScale());
    setZScale(other.zScale());
    setRotationOrigin(other.rotationOrigin());
    setRotationAxis(other.rotationAxis());
    setRotationAngle(other.rotationAngle());
    d->projectionMatrix = other.d->projectionMatrix;
    d->screen = other.d->screen;
}

ScreenPaintData& ScreenPaintData::operator=(const ScreenPaintData& rhs)
{
    setXScale(rhs.xScale());
    setYScale(rhs.yScale());
    setZScale(rhs.zScale());
    setXTranslation(rhs.xTranslation());
    setYTranslation(rhs.yTranslation());
    setZTranslation(rhs.zTranslation());
    setRotationOrigin(rhs.rotationOrigin());
    setRotationAxis(rhs.rotationAxis());
    setRotationAngle(rhs.rotationAngle());
    d->projectionMatrix = rhs.d->projectionMatrix;
    d->screen = rhs.d->screen;
    return *this;
}

ScreenPaintData& ScreenPaintData::operator*=(qreal scale)
{
    setXScale(this->xScale() * scale);
    setYScale(this->yScale() * scale);
    setZScale(this->zScale() * scale);
    return *this;
}

ScreenPaintData& ScreenPaintData::operator*=(const QVector2D& scale)
{
    setXScale(this->xScale() * scale.x());
    setYScale(this->yScale() * scale.y());
    return *this;
}

ScreenPaintData& ScreenPaintData::operator*=(const QVector3D& scale)
{
    setXScale(this->xScale() * scale.x());
    setYScale(this->yScale() * scale.y());
    setZScale(this->zScale() * scale.z());
    return *this;
}

ScreenPaintData& ScreenPaintData::operator+=(const QPointF& translation)
{
    return this->operator+=(QVector3D(translation));
}

ScreenPaintData& ScreenPaintData::operator+=(const QPoint& translation)
{
    return this->operator+=(QVector3D(translation));
}

ScreenPaintData& ScreenPaintData::operator+=(const QVector2D& translation)
{
    return this->operator+=(QVector3D(translation));
}

ScreenPaintData& ScreenPaintData::operator+=(const QVector3D& translation)
{
    translate(translation);
    return *this;
}

QMatrix4x4 ScreenPaintData::projectionMatrix() const
{
    return d->projectionMatrix;
}

EffectScreen* ScreenPaintData::screen() const
{
    return d->screen;
}

}
