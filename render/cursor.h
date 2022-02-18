/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QImage>
#include <QObject>
#include <QPoint>

namespace KWin
{

namespace input
{
class platform;
}

namespace render
{

class KWIN_EXPORT cursor : public QObject
{
    Q_OBJECT
public:
    bool enabled{false};

    cursor(input::platform* input);
    void set_enabled(bool enable);

    QImage image() const;
    QPoint hotspot() const;
    void mark_as_rendered();

Q_SIGNALS:
    void changed();

private:
    void rerender();

    input::platform* input;
    QRect last_rendered_geometry;
};

}
}
