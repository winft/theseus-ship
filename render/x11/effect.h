/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "main.h"
#include "utils/memory.h"

#include <kwineffects/effect.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

inline static xcb_atom_t register_support_property(QByteArray const& name)
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        return XCB_ATOM_NONE;
    }

    // get the atom for the name
    unique_cptr<xcb_intern_atom_reply_t> atomReply(xcb_intern_atom_reply(
        c, xcb_intern_atom_unchecked(c, false, name.size(), name.constData()), nullptr));
    if (!atomReply) {
        return XCB_ATOM_NONE;
    }

    // announce property on root window
    unsigned char dummy = 0;
    xcb_change_property(c,
                        XCB_PROP_MODE_REPLACE,
                        kwinApp()->x11RootWindow(),
                        atomReply->atom,
                        atomReply->atom,
                        8,
                        1,
                        &dummy);

    // TODO: add to _NET_SUPPORTED
    return atomReply->atom;
}

template<typename Effects>
void register_property_type(Effects& effects, long atom, bool reg)
{
    if (reg) {
        // initialized to 0 if not present yet
        ++effects.registered_atoms[atom];
    } else {
        if (--effects.registered_atoms[atom] == 0)
            effects.registered_atoms.remove(atom);
    }
}

template<typename Effects>
xcb_atom_t add_support_property(Effects& effects, QByteArray const& name)
{
    auto atom = register_support_property(name);
    if (atom == XCB_ATOM_NONE) {
        return atom;
    }

    effects.m_compositor->keepSupportProperty(atom);
    effects.m_managedProperties.insert(name, atom);
    register_property_type(effects, atom, true);

    return atom;
}

template<typename Effects>
xcb_atom_t announce_support_property(Effects& effects, Effect* effect, QByteArray const& name)
{
    auto it = effects.m_propertiesForEffects.find(name);

    if (it != effects.m_propertiesForEffects.end()) {
        // Property already registered for an effect. Just append Effect and return the stored atom.
        if (!it.value().contains(effect)) {
            it.value().append(effect);
        }
        return effects.m_managedProperties.value(name, XCB_ATOM_NONE);
    }

    effects.m_propertiesForEffects.insert(name, QList<Effect*>() << effect);

    return add_support_property(effects, name);
}

template<typename Effects>
void remove_support_property(Effects& effects, Effect* effect, QByteArray const& name)
{
    auto it = effects.m_propertiesForEffects.find(name);
    if (it == effects.m_propertiesForEffects.end()) {
        // Property is not registered.
        return;
    }

    if (!it.value().contains(effect)) {
        // Property is not registered for given effect.
        return;
    }

    it.value().removeAll(effect);

    if (!it.value().isEmpty()) {
        // Property still registered for some effect. Nothing to clean up.
        return;
    }

    auto atom = effects.m_managedProperties.take(name);

    register_property_type(effects, atom, false);
    effects.m_propertiesForEffects.remove(name);

    // Delayed removal.
    effects.m_compositor->removeSupportProperty(atom);
}

}
