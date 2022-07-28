/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "event.h"
#include "event_filter.h"
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

}
