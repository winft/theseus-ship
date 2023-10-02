/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/options.h"
#include "base/wayland/server.h"
#include "input/cursor.h"

#include "win/activation.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/wayland/space.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <KConfigGroup>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("screens", "[base]")
{
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("screens", operation_mode);
    setup.start();
    setup_wayland_connection();

    setup_wayland_connection();

    setup.set_outputs(1);
    set_current_output(0);
    cursor()->set_pos(QPoint(640, 512));

    SECTION("reconfigure")
    {
        struct data {
            std::string focus_policy;
            bool expected_default;
        };

        auto test_data = GENERATE(data{"ClickToFocus", false},
                                  data{"FocusFollowsMouse", true},
                                  data{"FocusUnderMouse", true},
                                  data{"FocusStrictlyUnderMouse", true});

        auto original_config = setup.base->config.main;
        auto& options = setup.base->space->options;

        QCOMPARE(options->get_current_output_follows_mouse(), false);

        auto config
            = KSharedConfig::openConfig("testScreens_testReconfigure", KConfig::SimpleConfig);
        config->group("Windows").writeEntry("FocusPolicy",
                                            QString::fromStdString(test_data.focus_policy));
        config->group("Windows").deleteEntry("ActiveMouseScreen");
        config->group("Windows").sync();
        config->sync();

        setup.base->config.main = config;
        options = std::make_unique<win::options>(config);
        options->loadConfig();

        QCOMPARE(options->get_current_output_follows_mouse(), test_data.expected_default);

        config->group("Windows").writeEntry("ActiveMouseScreen", !test_data.expected_default);
        config->sync();
        options->updateSettings();
        QCOMPARE(options->get_current_output_follows_mouse(), !test_data.expected_default);

        setup.base->config.main = original_config;
        options = std::make_unique<win::options>(original_config);
        options->loadConfig();
        QCOMPARE(options->get_current_output_follows_mouse(), false);
    }

    SECTION("size")
    {
        struct data {
            std::vector<QRect> geometries;
            QSize expected_size;
            int change_count;
        };

        auto test_data = GENERATE(
            // TODO(romangg): To test empty size does not make sense. Or does it?
            // data{{QRect()}, {0, 0}, 0},
            // cloned
            data{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}, {200, 100}, 2},
            // adjacent
            data{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}, {600, 400}, 4},
            // overlapping
            data{{QRect{-10, -20, 50, 100}, QRect{0, 0, 100, 200}}, {110, 220}, 3},
            // gap
            data{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}, {30, 60}, 3});

        QSignalSpy topology_spy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(topology_spy.isValid());

        setup.set_outputs(test_data.geometries);

        QCOMPARE(topology_spy.count(), 1);
        REQUIRE(setup.base->topology.size == test_data.expected_size);
    }

    SECTION("count")
    {
        auto const& base = setup.base;

        QSignalSpy output_added_spy(base.get(), &base::platform::output_added);
        QSignalSpy output_removed_spy(base.get(), &base::platform::output_removed);
        QVERIFY(output_added_spy.isValid());
        QVERIFY(output_removed_spy.isValid());

        QCOMPARE(base->outputs.size(), 1);

        // change to two screens
        std::vector<QRect> geometries{{QRect{0, 0, 100, 200}, QRect{100, 0, 100, 200}}};
        setup.set_outputs(geometries);

        QCOMPARE(output_added_spy.count(), 2);
        QCOMPARE(output_removed_spy.count(), 1);
        QCOMPARE(base->outputs.size(), 2);

        output_added_spy.clear();
        output_removed_spy.clear();

        // go back to one screen
        geometries.pop_back();
        setup.set_outputs(geometries);

        QCOMPARE(output_removed_spy.count(), 2);
        QCOMPARE(output_added_spy.count(), 1);
        QCOMPARE(base->outputs.size(), 1);

        // Setting the same geometries should emit the signal again.
        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        output_added_spy.clear();
        output_removed_spy.clear();

        setup.set_outputs(geometries);
        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(output_removed_spy.count(), 1);
        QCOMPARE(output_added_spy.count(), 1);
    }

    SECTION("intersecting")
    {
        struct data {
            std::vector<QRect> geometries;
            QRect test_geo;
            size_t expected_count;
        };

        auto test_data = GENERATE(
            // null-rect
            data{{QRect{0, 0, 100, 100}}, {}, 0},
            // non-overlapping
            data{{QRect{0, 0, 100, 100}}, {100, 0, 100, 100}, 0},
            // in-between
            data{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}, {15, 0, 2, 2}, 0},
            // gap-overlapping
            data{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}, {9, 10, 200, 200}, 2},
            // larger
            data{{QRect{0, 0, 100, 100}}, {-10, -10, 200, 200}, 1},
            // several
            data{{QRect{0, 0, 100, 100},
                  QRect{100, 0, 100, 100},
                  QRect{200, 100, 100, 100},
                  QRect{300, 100, 100, 100}},
                 {0, 0, 300, 300},
                 3});

        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        setup.set_outputs(test_data.geometries);

        QCOMPARE(changedSpy.count(), 1);

        auto const& outputs = setup.base->outputs;
        QCOMPARE(outputs.size(), test_data.geometries.size());
        REQUIRE(base::get_intersecting_outputs(outputs, test_data.test_geo).size()
                == test_data.expected_count);
    }

    SECTION("current")
    {
        struct data {
            int current;
            bool signal;
        };

        auto test_data = GENERATE(data{0, false}, data{1, true});

        auto& base = setup.base;
        setup.set_outputs(2);
        QCOMPARE(base->outputs.size(), 2);

        QSignalSpy current_changed_spy(base.get(), &base::platform::current_output_changed);
        QVERIFY(current_changed_spy.isValid());

        set_current_output(test_data.current);
        QCOMPARE(
            base::get_output_index(base->outputs, *win::get_current_output(*setup.base->space)),
            test_data.current);
        REQUIRE(current_changed_spy.isEmpty() != test_data.signal);
    }

    SECTION("current window")
    {
        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());
        QSignalSpy current_output_spy(setup.base.get(), &base::platform::current_output_changed);
        QVERIFY(current_output_spy.isValid());

        std::vector<QRect> geometries{{QRect{0, 0, 100, 100}, QRect{100, 0, 100, 100}}};
        setup.set_outputs(geometries);

        QCOMPARE(changedSpy.count(), 1);
        changedSpy.clear();

        // Create a window.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface, QSize(100, 50), Qt::blue);
        flush_wayland_connection();
        QVERIFY(clientAddedSpy.wait());
        auto client = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(client);

        win::move(client, QPoint(101, 0));
        QCOMPARE(setup.base->space->stacking.active, space::window_t(client));
        win::unset_active_window(*setup.base->space);
        QVERIFY(!setup.base->space->stacking.active);

        QCOMPARE(win::get_current_output(*setup.base->space),
                 base::get_output(setup.base->outputs, 0));

        // it's not the active client, so changing won't work
        win::set_current_output_by_window(*setup.base, *client);
        QVERIFY(changedSpy.isEmpty());
        QVERIFY(current_output_spy.isEmpty());

        auto output = base::get_output(setup.base->outputs, 0);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);

        // making the client active should affect things
        win::set_active(client, true);
        win::set_active_window(*setup.base->space, *client);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), client);

        // first of all current should be changed just by the fact that there is an active client
        output = base::get_output(setup.base->outputs, 1);
        QVERIFY(output);
        QCOMPARE(client->topo.central_output, output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);

        // but also calling setCurrent should emit the changed signal
        win::set_current_output_by_window(*setup.base, *client);
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(current_output_spy.count(), 1);

        output = base::get_output(setup.base->outputs, 1);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);

        // setting current with the same client again should not change, though
        win::set_current_output_by_window(*setup.base, *client);
        QCOMPARE(changedSpy.count(), 0);
        QCOMPARE(current_output_spy.count(), 1);

        // and it should even still be on screen 1 if we make the client non-current again
        win::unset_active_window(*setup.base->space);
        win::set_active(client, false);

        output = base::get_output(setup.base->outputs, 1);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);
    }

    SECTION("current with follows mouse")
    {
        struct data {
            std::vector<QRect> geometries;
            QPoint cursor_pos;
            int expected;
        };

        auto test_data = GENERATE(
            // TODO(romangg): To test empty size does not make sense. Or does it?
            // data{{QRect()}, {100, 100}, 0},
            // cloned
            data{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}, {50, 50}, 0},
            // adjacent-0
            data{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}, {199, 99}, 0},
            // adjacent-1
            data{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}, {200, 100}, 1},
            // gap
            data{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}, {15, 30}, 0});

        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("ActiveMouseScreen", true);
        group.sync();
        win::space_reconfigure(*setup.base->space);

        pointer_motion_absolute(QPointF(0, 0), 1);

        auto output = base::get_output(setup.base->outputs, 0);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);

        setup.set_outputs(test_data.geometries);
        QCOMPARE(changedSpy.count(), 1);

        pointer_motion_absolute(test_data.cursor_pos, 2);

        output = base::get_output(setup.base->outputs, test_data.expected);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);
    }

    SECTION("current point")
    {
        struct data {
            std::vector<QRect> geometries;
            QPoint cursor_pos;
            int expected;
        };

        auto test_data = GENERATE(
            // TODO(romangg): To test empty size does not make sense. Or does it?
            // data{{QRect()}, {100, 100}, 0},
            // cloned
            data{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}, {50, 50}, 0},
            // adjacent-0
            data{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}, {199, 99}, 0},
            // adjacent-1
            data{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}, {200, 100}, 1},
            // gap
            data{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}, {15, 30}, 1});

        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("ActiveMouseScreen", false);
        group.sync();
        win::space_reconfigure(*setup.base->space);

        setup.set_outputs(test_data.geometries);
        QCOMPARE(changedSpy.count(), 1);

        base::set_current_output_by_position(*setup.base, test_data.cursor_pos);

        auto output = base::get_output(setup.base->outputs, test_data.expected);
        QVERIFY(output);
        QCOMPARE(win::get_current_output(*setup.base->space), output);
    }
}

}
