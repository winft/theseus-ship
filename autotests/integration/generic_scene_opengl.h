/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "lib/setup.h"

namespace KWin::detail::test
{

inline std::unique_ptr<setup> generic_scene_opengl_get_setup(std::string const& test_name,
                                                             std::string const& env_var)
{
    qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("KWIN_COMPOSE", env_var.c_str());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);

    auto setup = std::make_unique<test::setup>(test_name);
    setup->start();

    auto plugins = KConfigGroup(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup->base->render).listOfKnownEffects();

    for (QString const& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup->base->config.main = config;
    QVERIFY(setup->base->render->compositor);

    auto& scene = setup->base->render->compositor->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());
    REQUIRE(!setup->base->render->is_sw_compositing());

    return setup;
}

}
