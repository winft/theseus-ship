/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "kwin_export.h"

#include <QObject>

namespace KWin::input
{

class keyboard;
class redirect;

class KWIN_EXPORT keyboard_redirect : public QObject
{
    Q_OBJECT
public:
    explicit keyboard_redirect(input::redirect* parent);
    ~keyboard_redirect() override;

    virtual void update();

    virtual void process_key(key_event const& event);
    virtual void process_key_repeat(key_event const& event);

    virtual void process_modifiers(modifiers_event const& event);

protected:
    input::redirect* redirect;
};

}
