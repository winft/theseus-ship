/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KConfigGroup>
#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("keymap creation failure", "[input]")
{
    // situation for for BUG 381210
    // this will fail to create keymap
    qputenv("XKB_DEFAULT_RULES", "no");
    qputenv("XKB_DEFAULT_MODEL", "no");
    qputenv("XKB_DEFAULT_LAYOUT", "no");
    qputenv("XKB_DEFAULT_VARIANT", "no");
    qputenv("XKB_DEFAULT_OPTIONS", "no");

    test::setup setup("keymap-create-fail");
    setup.start();

    setup.base->mod.input->xkb.setConfig(KSharedConfig::openConfig({}, KConfig::SimpleConfig));

    auto layoutGroup = setup.base->mod.input->config.xkb->group("Layout");
    layoutGroup.writeEntry("LayoutList", QStringLiteral("no"));
    layoutGroup.writeEntry("Model", "no");
    layoutGroup.writeEntry("Options", "no");
    layoutGroup.sync();

    setup_wayland_connection();

    // now create the crashing condition
    // which is sending in a pointer event
    pointer_button_pressed(BTN_LEFT, 0);
    pointer_button_released(BTN_LEFT, 1);
}

}
