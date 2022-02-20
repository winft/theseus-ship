/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016, 2017 Martin Gräßlin <mgraesslin@kde.org>

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

#include "input/event_spy.h"

#include "kwin_export.h"

#include <QObject>
#include <QSet>

namespace KWin::input
{

class KWIN_EXPORT modifier_only_shortcuts_spy : public QObject, public event_spy
{
    Q_OBJECT
public:
    explicit modifier_only_shortcuts_spy();
    ~modifier_only_shortcuts_spy() override;

    void button(button_event const& event) override;
    void axis(axis_event const& event) override;
    void key(key_event const& event) override;

    void reset()
    {
        m_modifier = Qt::NoModifier;
    }

private:
    Qt::KeyboardModifier m_modifier = Qt::NoModifier;
    Qt::KeyboardModifiers m_cachedMods;
    Qt::MouseButtons m_pressedButtons;
    QSet<quint32> m_pressedKeys;
};

}
