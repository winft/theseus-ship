/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2010 Rohan Prabhu <rohan@rohanprabhu.com>

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

#include "meta.h"
#include "window_wrapper.h"

#include <QRect>
#include <QtScript/QScriptEngine>

using namespace KWin::MetaScripting;

// Meta for QPoint object
QScriptValue Point::toScriptValue(QScriptEngine* eng, const QPoint& point)
{
    QScriptValue temp = eng->newObject();
    temp.setProperty(QStringLiteral("x"), point.x());
    temp.setProperty(QStringLiteral("y"), point.y());
    return temp;
}

void Point::fromScriptValue(const QScriptValue& obj, QPoint& point)
{
    QScriptValue x = obj.property(QStringLiteral("x"), QScriptValue::ResolveLocal);
    QScriptValue y = obj.property(QStringLiteral("y"), QScriptValue::ResolveLocal);

    if (!x.isUndefined() && !y.isUndefined()) {
        point.setX(x.toInt32());
        point.setY(y.toInt32());
    }
}
// End of meta for QPoint object

// Meta for QSize object
QScriptValue Size::toScriptValue(QScriptEngine* eng, const QSize& size)
{
    QScriptValue temp = eng->newObject();
    temp.setProperty(QStringLiteral("w"), size.width());
    temp.setProperty(QStringLiteral("h"), size.height());
    return temp;
}

void Size::fromScriptValue(const QScriptValue& obj, QSize& size)
{
    QScriptValue w = obj.property(QStringLiteral("w"), QScriptValue::ResolveLocal);
    QScriptValue h = obj.property(QStringLiteral("h"), QScriptValue::ResolveLocal);

    if (!w.isUndefined() && !h.isUndefined()) {
        size.setWidth(w.toInt32());
        size.setHeight(h.toInt32());
    }
}
// End of meta for QSize object

// Meta for QRect object. Just a temporary measure, hope to
// add a much better wrapping of the QRect object soon
QScriptValue Rect::toScriptValue(QScriptEngine* eng, const QRect& rect)
{
    QScriptValue temp = eng->newObject();
    temp.setProperty(QStringLiteral("x"), rect.x());
    temp.setProperty(QStringLiteral("y"), rect.y());
    temp.setProperty(QStringLiteral("width"), rect.width());
    temp.setProperty(QStringLiteral("height"), rect.height());

    return temp;
}

void Rect::fromScriptValue(const QScriptValue& obj, QRect &rect)
{
    QScriptValue w = obj.property(QStringLiteral("width"), QScriptValue::ResolveLocal);
    QScriptValue h = obj.property(QStringLiteral("height"), QScriptValue::ResolveLocal);
    QScriptValue x = obj.property(QStringLiteral("x"), QScriptValue::ResolveLocal);
    QScriptValue y = obj.property(QStringLiteral("y"), QScriptValue::ResolveLocal);

    if (!w.isUndefined() && !h.isUndefined() && !x.isUndefined() && !y.isUndefined()) {
        rect.setX(x.toInt32());
        rect.setY(y.toInt32());
        rect.setWidth(w.toInt32());
        rect.setHeight(h.toInt32());
    }
}
// End of meta for QRect object

// Other helper functions
void KWin::MetaScripting::registration(QScriptEngine* eng)
{
    qScriptRegisterMetaType<QPoint>(eng, Point::toScriptValue, Point::fromScriptValue);
    qScriptRegisterMetaType<QSize>(eng, Size::toScriptValue, Size::fromScriptValue);
    qScriptRegisterMetaType<QRect>(eng, Rect::toScriptValue, Rect::fromScriptValue);

    qScriptRegisterSequenceMetaType<QStringList>(eng);
}
