/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2020 Roman Gilg <subdiff@gmail.org>

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
#include "x11eventfilter.h"

namespace KWin
{
class X11StandalonePlatform;

class RandrFilter : public X11EventFilter
{
public:
    explicit RandrFilter(X11StandalonePlatform* backend);

    bool event(xcb_generic_event_t *event) override;

private:
    X11StandalonePlatform* m_backend;
};

}
