/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "render/effect_loader.h"
#include "script/platform.h"
#include "script/script.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#define private public
#include "win/screen_edges.h"
#undef private

#include <KConfigGroup>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("screen edge script", "[script]")
{
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("screen-edge-script", operation_mode);

    // empty config to have defaults
    auto config = setup.base->config.main;

    // disable all effects to prevent them grabbing edges
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render->compositor).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    // disable electric border pushback
    config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 0);
    config->group("TabBox").writeEntry("TouchBorderActivate", int(ElectricNone));
    config->sync();

    setup.start();
    QVERIFY(setup.base->space->scripting);

    setup.base->space->edges->time_threshold = 0;
    setup.base->space->edges->reactivate_threshold = 0;

    auto triggerConfigReload = [&]() { win::space_reconfigure(*setup.base->space); };

    SECTION("edge")
    {
        struct data {
            ElectricBorder edge;
            QPoint trigger_pos;
        };

        auto test_data = GENERATE(data{ElectricTop, {512, 0}},
                                  data{ElectricTopRight, {1279, 0}},
                                  data{ElectricRight, {1279, 512}},
                                  data{ElectricBottomRight, {1279, 1023}},
                                  data{ElectricBottom, {512, 1023}},
                                  data{ElectricBottomLeft, {0, 1023}},
                                  data{ElectricLeft, {0, 512}},
                                  data{ElectricTopLeft, {0, 0}});

        const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedge.js");
        QVERIFY(!scriptToLoad.isEmpty());

        // mock the config
        auto config = setup.base->config.main;
        config->group(QLatin1String("Script-") + scriptToLoad)
            .writeEntry("Edge", int(test_data.edge));
        config->sync();

        QVERIFY(!setup.base->space->scripting->isScriptLoaded(scriptToLoad));
        const int id = setup.base->space->scripting->loadScript(scriptToLoad);
        QVERIFY(id != -1);
        QVERIFY(setup.base->space->scripting->isScriptLoaded(scriptToLoad));
        auto s = setup.base->space->scripting->findScript(scriptToLoad);
        QVERIFY(s);
        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        QVERIFY(runningChangedSpy.isValid());
        s->run();
        QVERIFY(runningChangedSpy.wait());
        QCOMPARE(runningChangedSpy.count(), 1);
        QCOMPARE(runningChangedSpy.first().first().toBool(), true);

        // triggering the edge will result in show desktop being triggered
        QSignalSpy showDesktopSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::showingDesktopChanged);
        QVERIFY(showDesktopSpy.isValid());

        // trigger the edge
        cursor()->set_pos(test_data.trigger_pos);
        QCOMPARE(showDesktopSpy.count(), 1);
        QVERIFY(setup.base->space->showing_desktop);
    }

    SECTION("touch edge")
    {
        struct data {
            ElectricBorder edge;
            QPoint trigger_pos;
            QPoint motion_pos;
        };

        auto test_data = GENERATE(data{ElectricTop, {50, 0}, {50, 500}},
                                  data{ElectricRight, {1279, 50}, {500, 50}},
                                  data{ElectricBottom, {512, 1023}, {}},
                                  data{ElectricLeft, {0, 50}, {500, 50}});

        const QString scriptToLoad = QFINDTESTDATA("./scripts/touchScreenedge.js");
        QVERIFY(!scriptToLoad.isEmpty());

        // mock the config
        auto config = setup.base->config.main;
        config->group(QLatin1String("Script-") + scriptToLoad)
            .writeEntry("Edge", int(test_data.edge));
        config->sync();

        QVERIFY(!setup.base->space->scripting->isScriptLoaded(scriptToLoad));
        auto const id = setup.base->space->scripting->loadScript(scriptToLoad);
        QVERIFY(id != -1);
        QVERIFY(setup.base->space->scripting->isScriptLoaded(scriptToLoad));
        auto s = setup.base->space->scripting->findScript(scriptToLoad);
        QVERIFY(s);
        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        QVERIFY(runningChangedSpy.isValid());
        s->run();
        QVERIFY(runningChangedSpy.wait());
        QCOMPARE(runningChangedSpy.count(), 1);
        QCOMPARE(runningChangedSpy.first().first().toBool(), true);
        // triggering the edge will result in show desktop being triggered
        QSignalSpy showDesktopSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::showingDesktopChanged);
        QVERIFY(showDesktopSpy.isValid());

        // trigger the edge
        quint32 timestamp = 0;
        touch_down(0, test_data.trigger_pos, timestamp++);
        touch_motion(0, test_data.motion_pos, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(showDesktopSpy.wait());
        QCOMPARE(showDesktopSpy.count(), 1);
        QVERIFY(setup.base->space->showing_desktop);
    }

    SECTION("edge unregister")
    {
        const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedgeunregister.js");
        QVERIFY(!scriptToLoad.isEmpty());

        setup.base->space->scripting->loadScript(scriptToLoad);
        auto s = setup.base->space->scripting->findScript(scriptToLoad);
        auto configGroup = s->config();
        configGroup.writeEntry("Edge", int(KWin::ElectricLeft));
        configGroup.sync();
        const QPoint triggerPos = QPoint(0, 512);

        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        s->run();
        QVERIFY(runningChangedSpy.wait());

        QSignalSpy showDesktopSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::showingDesktopChanged);
        QVERIFY(showDesktopSpy.isValid());

        // trigger the edge
        cursor()->set_pos(triggerPos);

        if (operation_mode == base::operation_mode::xwayland) {
            // TODO(romangg): This test fails with Xwayland enabled. Fix it!
            return;
        }

        QCOMPARE(showDesktopSpy.count(), 1);

        // reset
        cursor()->set_pos(500, 500);
        win::toggle_show_desktop(*setup.base->space);
        showDesktopSpy.clear();

        // trigger again, to show that retriggering works
        cursor()->set_pos(triggerPos);
        QCOMPARE(showDesktopSpy.count(), 1);

        // reset
        cursor()->set_pos(500, 500);
        win::toggle_show_desktop(*setup.base->space);
        showDesktopSpy.clear();

        // make the script unregister the edge
        configGroup.writeEntry("mode", "unregister");
        triggerConfigReload();
        cursor()->set_pos(triggerPos);
        QCOMPARE(showDesktopSpy.count(), 0); // not triggered

        // force the script to unregister a non-registered edge to prove it doesn't explode
        triggerConfigReload();
    }

    SECTION("declarative touch edge")
    {
        const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedgetouch.qml");
        QVERIFY(!scriptToLoad.isEmpty());
        QVERIFY(setup.base->space->scripting->loadDeclarativeScript(scriptToLoad) != -1);
        QVERIFY(setup.base->space->scripting->isScriptLoaded(scriptToLoad));

        auto s = setup.base->space->scripting->findScript(scriptToLoad);
        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        s->run();
        QTRY_COMPARE(runningChangedSpy.count(), 1);

        QSignalSpy showDesktopSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::showingDesktopChanged);
        QVERIFY(showDesktopSpy.isValid());

        // Trigger the edge through touch
        quint32 timestamp = 0;
        touch_down(0, QPointF(0, 50), timestamp++);
        touch_motion(0, QPointF(500, 50), timestamp++);
        touch_up(0, timestamp++);

        QVERIFY(showDesktopSpy.wait());
    }

    // try to unload the script
    const QStringList scripts = {QFINDTESTDATA("./scripts/screenedge.js"),
                                 QFINDTESTDATA("./scripts/screenedgeunregister.js"),
                                 QFINDTESTDATA("./scripts/touchScreenedge.js")};
    for (const QString& script : scripts) {
        if (!script.isEmpty()) {
            if (setup.base->space->scripting->isScriptLoaded(script)) {
                QVERIFY(setup.base->space->scripting->unloadScript(script));
                QTRY_VERIFY(!setup.base->space->scripting->isScriptLoaded(script));
            }
        }
    }
}

}
