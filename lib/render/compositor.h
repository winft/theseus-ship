/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRegion>

namespace KWin::render
{

template<typename Compositor>
void full_repaint(Compositor& comp)
{
    auto const& space_size = comp.base.topology.size;
    comp.addRepaint(QRegion(0, 0, space_size.width(), space_size.height()));
}

}
