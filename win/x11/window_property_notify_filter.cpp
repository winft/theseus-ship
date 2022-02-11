/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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
#include "window_property_notify_filter.h"

#include "render/effects.h"
#include "render/window.h"
#include "win/space.h"
#include "win/x11/window.h"

namespace KWin::win::x11
{

window_property_notify_filter::window_property_notify_filter(render::effects_handler_impl* effects)
    : base::x11::event_filter(QVector<int>{XCB_PROPERTY_NOTIFY})
    , m_effects(effects)
{
}

bool window_property_notify_filter::event(xcb_generic_event_t* event)
{
    const auto* pe = reinterpret_cast<xcb_property_notify_event_t*>(event);
    if (!m_effects->isPropertyTypeRegistered(pe->atom)) {
        return false;
    }
    if (pe->window == kwinApp()->x11RootWindow()) {
        Q_EMIT m_effects->propertyNotify(nullptr, pe->atom);
    } else if (const auto c
               = workspace()->findClient(win::x11::predicate_match::window, pe->window)) {
        Q_EMIT m_effects->propertyNotify(c->render->effect.get(), pe->atom);
    } else if (const auto c = workspace()->findUnmanaged(pe->window)) {
        Q_EMIT m_effects->propertyNotify(c->render->effect.get(), pe->atom);
    }
    return false;
}

}
