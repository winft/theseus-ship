/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/pointer_redirect.h"

namespace KWin::input::x11
{

class redirect;

class pointer_redirect : public input::pointer_redirect
{
public:
    explicit pointer_redirect(x11::redirect* redirect)
        : input::pointer_redirect(redirect)
        , redirect{redirect}
    {
    }

    QPointF pos() const override
    {
        return {};
    }

    void setEffectsOverrideCursor(Qt::CursorShape /*shape*/) override
    {
    }
    void removeEffectsOverrideCursor() override
    {
    }

    void setEnableConstraints(bool /*set*/) override
    {
    }

    void process_button(button_event const& event) override
    {
        pointer_redirect_process_button_spies(*this, event);
    }

    x11::redirect* redirect;
};

}
