/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "event.h"
#include "event_filter.h"
#include "event_spy.h"
#include "keyboard.h"
#include "redirect.h"
#include "xkb/keyboard.h"

#include <KGlobalAccel>
#include <QKeyEvent>

namespace KWin::input
{

keyboard_redirect::keyboard_redirect(input::redirect* redirect)
    : QObject()
    , redirect(redirect)
{
}

keyboard_redirect::~keyboard_redirect() = default;

void keyboard_redirect::update()
{
}

void keyboard_redirect::process_key(key_event const& event)
{
    event.base.dev->xkb->update_key(event.keycode, event.state);
    redirect->processSpies(std::bind(&event_spy::key, std::placeholders::_1, event));
}

void keyboard_redirect::process_key_repeat(key_event const& event)
{
    redirect->processSpies(std::bind(&event_spy::key_repeat, std::placeholders::_1, event));
}

void keyboard_redirect::process_modifiers(modifiers_event const& /*event*/)
{
}

void keyboard_redirect::processKeymapChange(int /*fd*/, uint32_t /*size*/)
{
}

}
