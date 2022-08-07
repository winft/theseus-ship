/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <memory>

namespace KWin
{

namespace input
{
class platform;
}

namespace render
{

class platform;

class KWIN_EXPORT cursor_qobject : public QObject
{
    Q_OBJECT
public:
    cursor_qobject();

Q_SIGNALS:
    void changed();
};

class KWIN_EXPORT cursor
{
public:
    cursor(render::platform& platform, input::platform* input);
    void set_enabled(bool enable);

    QImage image() const;
    QPoint hotspot() const;
    void mark_as_rendered();

    std::unique_ptr<cursor_qobject> qobject;
    bool enabled{false};

private:
    void rerender();

    render::platform& platform;
    input::platform* input;
    QRect last_rendered_geometry;

    struct {
        QMetaObject::Connection pos;
        QMetaObject::Connection image;
    } notifiers;
};

}
}
