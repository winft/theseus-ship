/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (c) 2003, Karol Szwed <kszwed@kde.org>

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

#include "xcbutils.h"

#include <QLabel>

namespace KWin::win::x11
{

class geometry_tip : public QLabel
{
    Q_OBJECT
public:
    geometry_tip(Xcb::GeometryHints const* xSizeHints);
    ~geometry_tip() override;
    void set_geometry(QRect const& geom);

private:
    Xcb::GeometryHints const* sizeHints;
};

}
