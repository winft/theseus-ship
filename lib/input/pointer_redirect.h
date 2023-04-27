/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "event_spy.h"

namespace KWin::input
{

template<typename Pointer>
void pointer_redirect_process_button_spies(Pointer& ptr, button_event const& event)
{
    using redirect_t = std::remove_pointer_t<decltype(ptr.redirect)>;
    process_spies(ptr.redirect->m_spies,
                  std::bind(&event_spy<redirect_t>::button, std::placeholders::_1, event));
}

}
