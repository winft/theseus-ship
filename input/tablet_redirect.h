/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Aleix Pol Gonzalez <aleixpol@kde.org>

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
#pragma once

#include "device_redirect.h"
#include "redirect.h"

#include <QPointF>

namespace KWin
{
class Toplevel;

namespace input
{

class tablet_redirect : public device_redirect
{
public:
    explicit tablet_redirect(input::redirect* redirect)
        : device_redirect(redirect)
    {
    }

    virtual void tabletToolEvent(redirect::TabletEventType /*type*/,
                                 QPointF const& /*pos*/,
                                 qreal /*pressure*/,
                                 int /*xTilt*/,
                                 int /*yTilt*/,
                                 qreal /*rotation*/,
                                 bool /*tipDown*/,
                                 bool /*tipNear*/,
                                 quint64 /*serialId*/,
                                 quint64 /*toolId*/,
                                 void* /*device*/)
    {
    }
    virtual void tabletToolButtonEvent(uint /*button*/, bool /*isPressed*/)
    {
    }
    virtual void tabletPadButtonEvent(uint /*button*/, bool /*isPressed*/)
    {
    }
    virtual void tabletPadStripEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
    }
    virtual void tabletPadRingEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
    }
};

}
}
