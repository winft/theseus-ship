/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard_redirect.h"

#include <memory>

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

    void init();
    void update() override;

    void process_key(key_event const& event) override;
    void process_key_repeat(key_event const& event) override;

    void process_modifiers(modifiers_event const& event) override;
    void processKeymapChange(int fd, uint32_t size) override;

private:
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy* modifiers_spy{nullptr};
    std::unique_ptr<keyboard_layout_spy> m_keyboardLayout;
};

}
}
