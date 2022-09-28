/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/device_redirect.h"
#include "input/pointer_redirect.h"

namespace KWin::input::x11
{

template<typename Redirect>
class pointer_redirect
{
public:
    explicit pointer_redirect(Redirect* redirect)
        : qobject{std::make_unique<device_redirect_qobject>()}
        , redirect{redirect}
    {
    }

    QPointF pos() const
    {
        return {};
    }

    void setEffectsOverrideCursor(Qt::CursorShape /*shape*/)
    {
    }

    void removeEffectsOverrideCursor()
    {
    }

    void setEnableConstraints(bool /*set*/)
    {
    }

    void process_button(button_event const& event)
    {
        pointer_redirect_process_button_spies(*this, event);
    }

    std::unique_ptr<device_redirect_qobject> qobject;
    Redirect* redirect;
};

}
