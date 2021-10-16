/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "event_filter.h"
#include "input/event.h"
#include "input/event_spy.h"

#include <KGlobalAccel>
#include <QKeyEvent>

namespace KWin::input
{

keyboard_redirect::keyboard_redirect(input::redirect* redirect)
    : QObject()
    , m_xkb{std::make_unique<input::xkb>()}
    , redirect(redirect)
{
    connect(m_xkb.get(), &input::xkb::ledsChanged, this, &keyboard_redirect::ledsChanged);
}

keyboard_redirect::~keyboard_redirect() = default;

void keyboard_redirect::init()
{
}

input::xkb* keyboard_redirect::xkb() const
{
    return m_xkb.get();
}
Qt::KeyboardModifiers keyboard_redirect::modifiers() const
{
    return m_xkb->modifiers();
}
Qt::KeyboardModifiers keyboard_redirect::modifiersRelevantForGlobalShortcuts() const
{
    return m_xkb->modifiersRelevantForGlobalShortcuts();
}

void keyboard_redirect::update()
{
}

void keyboard_redirect::process_key(key_event const& event)
{
    m_xkb->updateKey(event.keycode, (redirect::KeyboardKeyState)event.state);
    redirect->processSpies(std::bind(&event_spy::key, std::placeholders::_1, event));
}

void keyboard_redirect::process_key_repeat(uint32_t key, uint32_t time)
{
    auto event = key_event{key, button_state::pressed, false, nullptr, time};
    redirect->processSpies(std::bind(&event_spy::key_repeat, std::placeholders::_1, event));
}

void keyboard_redirect::process_modifiers(modifiers_event const& event)
{
    processModifiers(event.depressed, event.latched, event.locked, event.group);
}

void keyboard_redirect::processModifiers(uint32_t /*modsDepressed*/,
                                         uint32_t /*modsLatched*/,
                                         uint32_t /*modsLocked*/,
                                         uint32_t /*group*/)
{
}

void keyboard_redirect::processKeymapChange(int /*fd*/, uint32_t /*size*/)
{
}

}
