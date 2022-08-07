/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "event_spy.h"
#include "keyboard.h"

#include "kwin_export.h"

#include <QObject>

namespace KWin::input
{

class keyboard;
class redirect;

template<typename Keyboard>
void keyboard_redirect_prepare_key(Keyboard& keys, key_event const& event)
{
    event.base.dev->xkb->update_key(event.keycode, event.state);
    process_spies(keys.redirect->m_spies, std::bind(&event_spy::key, std::placeholders::_1, event));
}

class KWIN_EXPORT keyboard_redirect_qobject : public QObject
{
public:
    ~keyboard_redirect_qobject() override;
};

class keyboard_redirect
{
public:
    explicit keyboard_redirect(input::redirect* redirect)
        : qobject{std::make_unique<keyboard_redirect_qobject>()}
        , redirect(redirect)
    {
    }

    virtual ~keyboard_redirect() = default;

    virtual void update() = 0;
    virtual void process_key(key_event const& event) = 0;
    virtual void process_modifiers(modifiers_event const& event) = 0;

    std::unique_ptr<keyboard_redirect_qobject> qobject;
    input::redirect* redirect;
};

}
