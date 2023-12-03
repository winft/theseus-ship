/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <base/wayland/app_singleton.h>
#include <base/wayland/platform.h>
#include <input/wayland/platform.h>
#include <script/platform.h>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

int main(int argc, char* argv[])
{
    using namespace KWin;

    base::wayland::app_singleton app(argc, argv);

    using base_t = base::wayland::platform<>;
    base_t base({.config = base::config(KConfig::OpenFlag::FullConfig, "kwinft-minimalrc")});

    base.options = base::create_options(base::operation_mode::wayland, base.config.main);
    base.mod.render = std::make_unique<base_t::render_t>(base);
    base.mod.input = std::make_unique<input::wayland::platform<base_t>>(
        base, input::config(KConfig::NoGlobals));
    base.mod.space = std::make_unique<base_t::space_t>(*base.mod.render, *base.mod.input);

    return base::wayland::exec(base, app);
}
