/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2018, 2021 Roman Gilg <subdiff@gmail.com>

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
#include "output.h"

namespace KWin::base
{

void output::set_enabled(bool /*enable*/)
{
}

void output::apply_changes(Wrapland::Server::OutputChangesetV1 const* /*changeset*/)
{
}

bool output::is_internal() const
{
    return false;
}

qreal output::scale() const
{
    return 1;
}

QSize output::physical_size() const
{
    return QSize();
}

int output::gamma_ramp_size() const
{
    return 0;
}

bool output::set_gamma_ramp(gamma_ramp const& /*gamma*/)
{
    return false;
}

void output::update_dpms(DpmsMode /*mode*/)
{
}

}
