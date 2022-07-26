/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_mode_manager.h"

namespace KWin::input::dbus
{

bool tablet_mode_manager::isTabletModeAvailable() const
{
    return m_detecting;
}

bool tablet_mode_manager::isTablet() const
{
    return m_isTabletMode;
}

void tablet_mode_manager::setIsTablet(bool tablet)
{
    if (m_isTabletMode == tablet) {
        return;
    }

    m_isTabletMode = tablet;
    Q_EMIT tabletModeChanged(tablet);
}

void tablet_mode_manager::setTabletModeAvailable(bool detecting)
{
    if (m_detecting != detecting) {
        m_detecting = detecting;
        Q_EMIT tabletModeAvailableChanged(detecting);
    }
}

}
