/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard_redirect.h"

#include <memory>

namespace KWin::input
{

namespace xkb
{
template<typename Xkb>
class layout_manager;
template<typename Platform>
class manager;
}

namespace wayland
{
class modifiers_changed_spy;
class redirect;

class KWIN_EXPORT keyboard_redirect : public input::keyboard_redirect
{
public:
    explicit keyboard_redirect(wayland::redirect* redirect);
    ~keyboard_redirect() override;

    void init();
    void update() override;

    void process_key(key_event const& event) override;
    void process_key_repeat(key_event const& event);

    void process_modifiers(modifiers_event const& event) override;

    wayland::redirect* redirect;

private:
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy* modifiers_spy{nullptr};

    using layout_manager_t = xkb::layout_manager<xkb::manager<platform>>;
    std::unique_ptr<layout_manager_t> layout_manager;
};

}
}
