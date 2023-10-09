/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/post/constants.h"
#include "render/post/night_color_manager.h"

#include <KConfigGroup>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("night color", "[render]")
{
    test::setup setup("night-color");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();

    SECTION("config read")
    {
        struct data {
            bool active;
            int mode;
        };

        auto test_data
            = GENERATE(data{true, 0}, data{true, 1}, data{true, 3}, data{false, 2}, data{false, 4});

        const bool activeDefault = true;
        const int modeDefault = 0;

        auto cfgGroup = setup.base->config.main->group("NightColor");

        cfgGroup.writeEntry("Active", activeDefault);
        cfgGroup.writeEntry("Mode", modeDefault);

        cfgGroup.sync();
        auto& manager = setup.base->render->night_color;
        manager->reconfigure();

        QCOMPARE(manager->data.enabled, activeDefault);
        QCOMPARE(manager->data.mode, modeDefault);

        cfgGroup.writeEntry("Active", test_data.active);
        cfgGroup.writeEntry("Mode", test_data.mode);
        cfgGroup.sync();

        manager->reconfigure();

        QCOMPARE(manager->data.enabled, test_data.active);
        if (test_data.mode > 3 || test_data.mode < 0) {
            QCOMPARE(manager->data.mode, 0);
        } else {
            QCOMPARE(manager->data.mode, test_data.mode);
        }
    }
}

}
