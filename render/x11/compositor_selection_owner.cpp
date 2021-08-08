/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor_selection_owner.h"

#include <kwinglobals.h>

namespace KWin::render::x11
{

compositor_selection_owner::compositor_selection_owner(char const* selection)
    : KSelectionOwner(selection, connection(), rootWindow())
    , m_owning(false)
{
    connect(this, &compositor_selection_owner::lostOwnership, this, [this] { m_owning = false; });
}
bool compositor_selection_owner::owning() const
{
    return m_owning;
}
void compositor_selection_owner::setOwning(bool own)
{
    m_owning = own;
}

}
