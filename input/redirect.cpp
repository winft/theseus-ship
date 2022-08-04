/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "pointer_redirect.h"
#include "touch_redirect.h"

#include "utils/algorithm.h"

namespace KWin::input
{

redirect::redirect(input::platform& platform, win::space& space)
    : qobject{std::make_unique<redirect_qobject>()}
    , platform{platform}
    , space{space}
{
    platform.redirect = this;
}

redirect::~redirect()
{
    auto const spies = m_spies;
    for (auto spy : spies) {
        delete spy;
    }
}

void redirect::installInputEventSpy(event_spy* spy)
{
    m_spies.push_back(spy);
}

void redirect::uninstallInputEventSpy(event_spy* spy)
{
    remove_all(m_spies, spy);
}

void redirect::cancelTouch()
{
    get_touch()->cancel();
}

Qt::MouseButtons redirect::qtButtonStates() const
{
    return get_pointer()->buttons();
}

QPointF redirect::globalPointer() const
{
    return get_pointer()->pos();
}

void redirect::startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                               QByteArray const& /*cursorName*/)
{
    callback(nullptr);
}

void redirect::startInteractivePositionSelection(std::function<void(QPoint const&)> callback)
{
    callback(QPoint(-1, -1));
}

bool redirect::isSelectingWindow() const
{
    return false;
}

}
