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

template<typename Redirect, typename Keyboard>
void keyboard_redirect_prepare_key(Keyboard& keys, key_event const& event)
{
    event.base.dev->xkb->update_key(event.keycode, event.state);
    process_spies(keys.redirect->m_spies,
                  std::bind(&event_spy<Redirect>::key, std::placeholders::_1, event));
}

class KWIN_EXPORT keyboard_redirect_qobject : public QObject
{
public:
    ~keyboard_redirect_qobject() override;
};

}
