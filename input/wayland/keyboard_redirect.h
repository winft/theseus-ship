/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard_redirect.h"

namespace KWin::input
{
class keyboard_layout_spy;

namespace wayland
{
class modifiers_changed_spy;
class redirect;

class KWIN_EXPORT keyboard_redirect : public input::keyboard_redirect
{
    Q_OBJECT
public:
    explicit keyboard_redirect(wayland::redirect* redirect);
    ~keyboard_redirect() override;

    void init() override;
    void update() override;

    void process_key(key_event const& event) override;
    void process_key_repeat(uint32_t key, uint32_t time) override;

    void processModifiers(uint32_t modsDepressed,
                          uint32_t modsLatched,
                          uint32_t modsLocked,
                          uint32_t group) override;
    void processKeymapChange(int fd, uint32_t size) override;

private:
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy* modifiers_spy{nullptr};
    keyboard_layout_spy* m_keyboardLayout{nullptr};
};

}
}
