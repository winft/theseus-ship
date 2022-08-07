/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 Roman Gilg <subdiff@gmail.com>

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
#include "event.h"

#include <QPointF>

namespace KWin::input
{
class touch;

class touch_redirect : public device_redirect
{
public:
    explicit touch_redirect(input::redirect* redirect)
        : device_redirect(redirect)
    {
    }

    virtual void process_down(touch_down_event const& /*event*/)
    {
    }
    virtual void process_up(touch_up_event const& /*event*/)
    {
    }
    virtual void process_motion(touch_motion_event const& /*event*/)
    {
    }
    virtual void cancel()
    {
    }
    virtual void frame()
    {
    }
    virtual void insertId(qint32 /*internalId*/, qint32 /*wraplandId*/)
    {
    }
    virtual void removeId(qint32 /*internalId*/)
    {
    }
    virtual qint32 mappedId(qint32 /*internalId*/)
    {
        return 0;
    }
    virtual void setDecorationPressId(qint32 /*id*/)
    {
    }
    virtual qint32 decorationPressId() const
    {
        return 0;
    }
    virtual void setInternalPressId(qint32 /*id*/)
    {
    }
    virtual qint32 internalPressId() const
    {
        return 0;
    }
};

}
