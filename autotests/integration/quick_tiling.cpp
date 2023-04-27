/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "script/platform.h"
#include "script/script.h"
#include "win/deco/bridge.h"
#include "win/deco/settings.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/wayland/space.h"
#include "win/window_operation.h"
#include "win/x11/window.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QTemporaryFile>
#include <QTextStream>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

namespace
{

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_disconnect);
}

Wrapland::Client::xdg_shell_states get_client_tiles(win::quicktiles tiles)
{
    Wrapland::Client::xdg_shell_states states;
    auto maximized{true};

    auto check_tile = [&](win::quicktiles tile, Wrapland::Client::xdg_shell_state state) {
        if (flags(tiles & tile)) {
            states |= state;
        } else {
            // When any tile is inactive, the state is not maximized.
            maximized = false;
        }
    };

    check_tile(win::quicktiles::left, Wrapland::Client::xdg_shell_state::tiled_left);
    check_tile(win::quicktiles::right, Wrapland::Client::xdg_shell_state::tiled_right);
    check_tile(win::quicktiles::top, Wrapland::Client::xdg_shell_state::tiled_top);
    check_tile(win::quicktiles::bottom, Wrapland::Client::xdg_shell_state::tiled_bottom);

    if (maximized) {
        states |= Wrapland::Client::xdg_shell_state::maximized;
    }
    return states;
}

}

TEST_CASE("quick tiling", "[win]")
{
    qputenv("XKB_DEFAULT_RULES", "evdev");

    test::setup setup("quick-tiling", base::operation_mode::xwayland);

    // set custom config which disables the Outline
    auto group = setup.base->config.main->group("Outline");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::xdg_decoration);

    auto get_x11_window_from_id
        = [&](uint32_t id) { return get_x11_window(setup.base->space->windows_map.at(id)); };

    SECTION("quick tiling")
    {
        using namespace Wrapland::Client;

        struct data {
            win::quicktiles mode;
            QRect expected_geo;
            QRect second_screen;
            win::quicktiles expected_mode_after_toggle;
        };

        auto test_data = GENERATE(data{win::quicktiles::left,
                                       {0, 0, 640, 1024},
                                       {1280, 0, 640, 1024},
                                       win::quicktiles::right},
                                  data{win::quicktiles::top,
                                       {0, 0, 1280, 512},
                                       {1280, 0, 1280, 512},
                                       win::quicktiles::top},
                                  data{win::quicktiles::right,
                                       {640, 0, 640, 1024},
                                       {1920, 0, 640, 1024},
                                       win::quicktiles::none},
                                  data{win::quicktiles::bottom,
                                       {0, 512, 1280, 512},
                                       {1280, 512, 1280, 512},
                                       win::quicktiles::bottom},
                                  data{win::quicktiles::left | win::quicktiles::top,
                                       {0, 0, 640, 512},
                                       {1280, 0, 640, 512},
                                       win::quicktiles::right | win::quicktiles::top},
                                  data{win::quicktiles::right | win::quicktiles::top,
                                       {640, 0, 640, 512},
                                       {1920, 0, 640, 512},
                                       win::quicktiles::none},
                                  data{win::quicktiles::left | win::quicktiles::bottom,
                                       {0, 512, 640, 512},
                                       {1280, 512, 640, 512},
                                       win::quicktiles::right | win::quicktiles::bottom},
                                  data{win::quicktiles::right | win::quicktiles::bottom,
                                       {640, 512, 640, 512},
                                       {1920, 512, 640, 512},
                                       win::quicktiles::none},
                                  data{win::quicktiles::maximize,
                                       {0, 0, 1280, 1024},
                                       {1280, 0, 1280, 1024},
                                       win::quicktiles::none});

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // Map the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);

        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());

        // We have to receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        win::set_quicktile_mode(c, test_data.mode, true);
        QCOMPARE(quickTileChangedSpy.count(), 1);

        // at this point the geometry did not yet change
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));

        // but quick tile mode already changed
        QCOMPARE(c->control->quicktiling, test_data.mode);

        // but we got requested a new geometry
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, test_data.expected_geo.size());
        REQUIRE(cfgdata.states == (get_client_tiles(test_data.mode) | xdg_shell_state::activated));

        // attach a new image
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, test_data.expected_geo.size(), Qt::red);

        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(c->geo.frame, test_data.expected_geo);

        // send window to other screen
        QCOMPARE(c->topo.central_output, setup.base->outputs.at(0));

        auto output = base::get_output(setup.base->outputs, 1);
        QVERIFY(output);
        win::send_to_screen(*setup.base->space, c, *output);
        QCOMPARE(c->topo.central_output, setup.base->outputs.at(1));

        // quick tile should not be changed
        QCOMPARE(c->control->quicktiling, test_data.mode);
        REQUIRE(c->geo.frame == test_data.second_screen);

        // now try to toggle again
        win::set_quicktile_mode(c, test_data.mode, true);
        REQUIRE(c->control->quicktiling == test_data.expected_mode_after_toggle);
    }

    SECTION("quick maximizing")
    {
        using namespace Wrapland::Client;

        auto mode = GENERATE(win::quicktiles::maximize, win::quicktiles::none);

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // Map the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

        // We have to receive a configure event upon becoming active.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        REQUIRE(cfgdata.states == xdg_shell_states(xdg_shell_state::activated));

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy maximizeChangedSpy(c->qobject.get(),
                                      &win::window_qobject::maximize_mode_changed);
        QVERIFY(maximizeChangedSpy.isValid());

        // Now quicktile-maximize.
        win::set_quicktile_mode(c, win::quicktiles::maximize, true);
        QCOMPARE(quickTileChangedSpy.count(), 1);

        // At this point the geometry did not yet change.
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        // but quick tile mode already changed
        QCOMPARE(c->control->quicktiling, win::quicktiles::maximize);
        QCOMPARE(c->geo.restore.max, QRect(0, 0, 100, 50));

        // But we got requested a new geometry.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        REQUIRE(cfgdata.states
                == xdg_shell_states(get_client_tiles(win::quicktiles::maximize)
                                    | xdg_shell_state::activated));

        // Attach a new image.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, cfgdata.size, Qt::red);

        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->geo.restore.max, QRect(0, 0, 100, 50));

        // client is now set to maximised
        QCOMPARE(maximizeChangedSpy.count(), 1);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::full);

        // go back to quick tile none
        win::set_quicktile_mode(c, mode, true);
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);
        QCOMPARE(quickTileChangedSpy.count(), 2);

        // geometry not yet changed
        QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->geo.restore.max, QRect());

        // we got requested a new geometry
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 3);

        cfgdata = shellSurface->get_configure_data();
        REQUIRE(cfgdata.size == QSize(100, 50));
        REQUIRE(cfgdata.states == xdg_shell_states(xdg_shell_state::activated));

        // render again
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, QSize(100, 50), Qt::yellow);

        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 2);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(maximizeChangedSpy.count(), 2);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);
    }

    SECTION("keyboard move")
    {
        using namespace Wrapland::Client;

        struct data {
            QPoint target;
            win::quicktiles expected_mode;
        };

        auto test_data
            = GENERATE(data{{2559, 24}, win::quicktiles::top | win::quicktiles::right},
                       data{{2559, 512}, win::quicktiles::right},
                       data{{2559, 1023}, win::quicktiles::bottom | win::quicktiles::right},
                       data{{0, 1023}, win::quicktiles::bottom | win::quicktiles::left},
                       data{{0, 512}, win::quicktiles::left},
                       data{{0, 24}, win::quicktiles::top | win::quicktiles::left});

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // let's render
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());

        win::perform_window_operation(c, base::options_qobject::UnrestrictedMoveOp);
        QCOMPARE(c, get_wayland_window(setup.base->space->move_resize_window));
        QCOMPARE(cursor()->pos(), QPoint(49, 24));

        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        while (cursor()->pos().x() > test_data.target.x()) {
            keyboard_key_pressed(KEY_LEFT, timestamp++);
            keyboard_key_released(KEY_LEFT, timestamp++);
        }
        while (cursor()->pos().x() < test_data.target.x()) {
            keyboard_key_pressed(KEY_RIGHT, timestamp++);
            keyboard_key_released(KEY_RIGHT, timestamp++);
        }
        while (cursor()->pos().y() < test_data.target.y()) {
            keyboard_key_pressed(KEY_DOWN, timestamp++);
            keyboard_key_released(KEY_DOWN, timestamp++);
        }
        while (cursor()->pos().y() > test_data.target.y()) {
            keyboard_key_pressed(KEY_UP, timestamp++);
            keyboard_key_released(KEY_UP, timestamp++);
        }
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_ENTER, timestamp++);
        keyboard_key_released(KEY_ENTER, timestamp++);
        QCOMPARE(cursor()->pos(), test_data.target);
        QVERIFY(!setup.base->space->move_resize_window);

        QCOMPARE(quickTileChangedSpy.count(), 1);
        REQUIRE(c->control->quicktiling == test_data.expected_mode);
    }

    SECTION("pointer move")
    {
        using namespace Wrapland::Client;

        struct data {
            QPoint target;
            win::quicktiles expected_mode;
        };

        auto test_data
            = GENERATE(data{{2559, 24}, win::quicktiles::top | win::quicktiles::right},
                       data{{2559, 512}, win::quicktiles::right},
                       data{{2559, 1023}, win::quicktiles::bottom | win::quicktiles::right},
                       data{{0, 1023}, win::quicktiles::bottom | win::quicktiles::left},
                       data{{0, 512}, win::quicktiles::left},
                       data{{0, 24}, win::quicktiles::top | win::quicktiles::left});

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        QVERIFY(shellSurface);

        // wait for the initial configure event
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        // let's render
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

        // we have to receive a configure event when the client becomes active
        QVERIFY(configureRequestedSpy.wait());
        QTRY_COMPARE(configureRequestedSpy.count(), 2);

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());

        win::perform_window_operation(c, base::options_qobject::UnrestrictedMoveOp);
        QCOMPARE(c, get_wayland_window(setup.base->space->move_resize_window));
        QCOMPARE(cursor()->pos(), QPoint(49, 24));
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 3);

        quint32 timestamp = 1;
        pointer_motion_absolute(test_data.target, timestamp++);
        pointer_button_pressed(BTN_LEFT, timestamp++);
        pointer_button_released(BTN_LEFT, timestamp++);
        QCOMPARE(cursor()->pos(), test_data.target);
        QVERIFY(!setup.base->space->move_resize_window);

        QCOMPARE(quickTileChangedSpy.count(), 1);
        REQUIRE(c->control->quicktiling == test_data.expected_mode);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 4);
        QVERIFY(!shellSurface->get_configure_data().size.isEmpty());
    }

    SECTION("touch move")
    {
        // test verifies that touch on decoration also allows quick tiling
        // see BUG: 390113
        using namespace Wrapland::Client;

        struct data {
            QPoint target;
            win::quicktiles expected_mode;
        };

        auto test_data
            = GENERATE(data{{2559, 24}, win::quicktiles::top | win::quicktiles::right},
                       data{{2559, 512}, win::quicktiles::right},
                       data{{2559, 1023}, win::quicktiles::bottom | win::quicktiles::right},
                       data{{0, 1023}, win::quicktiles::bottom | win::quicktiles::left},
                       data{{0, 512}, win::quicktiles::left},
                       data{{0, 24}, win::quicktiles::top | win::quicktiles::left});

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        QVERIFY(shellSurface);

        auto deco = get_client().interfaces.xdg_decoration->getToplevelDecoration(
            shellSurface.get(), shellSurface.get());
        QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
        QVERIFY(decoSpy.isValid());

        deco->setMode(XdgDecoration::Mode::ServerSide);
        QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);

        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());

        init_xdg_shell_toplevel(surface, shellSurface);
        QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
        QCOMPARE(configureRequestedSpy.count(), 1);
        QVERIFY(configureRequestedSpy.last().first().toSize().isEmpty());

        // let's render
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        auto c = render_and_wait_for_shown(surface, QSize(1000, 50), Qt::blue);

        QVERIFY(c);
        QVERIFY(win::decoration(c));
        auto const decoration = win::decoration(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame,
                 QRect(-decoration->borderLeft(),
                       0,
                       1000 + decoration->borderLeft() + decoration->borderRight(),
                       50 + decoration->borderTop() + decoration->borderBottom()));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);
        QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

        // we have to receive a configure event when the client becomes active
        QVERIFY(configureRequestedSpy.wait());
        QTRY_COMPARE(configureRequestedSpy.count(), 2);

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());

        quint32 timestamp = 1;
        touch_down(
            0,
            QPointF(c->geo.frame.center().x(), c->geo.frame.y() + decoration->borderTop() / 2),
            timestamp++);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(c, get_wayland_window(setup.base->space->move_resize_window));
        QCOMPARE(configureRequestedSpy.count(), 3);

        touch_motion(0, test_data.target, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(!setup.base->space->move_resize_window);

        // When there are no borders, there is no change to them when quick-tiling.
        // TODO: we should test both cases with fixed fake decoration for autotests.
        auto const hasBorders
            = setup.base->space->deco->settings()->borderSize() != KDecoration2::BorderSize::None;

        QCOMPARE(quickTileChangedSpy.count(), 1);
        REQUIRE(c->control->quicktiling == test_data.expected_mode);
        QVERIFY(configureRequestedSpy.wait());
        TRY_REQUIRE(configureRequestedSpy.count() == (hasBorders ? 5 : 4));

        QVERIFY(!shellSurface->get_configure_data().size.isEmpty());
    }

    SECTION("x11 quick tiling")
    {
        struct data {
            win::quicktiles mode;
            QRect expected_geo;
            int expected_screen;
            win::quicktiles expected_mode;
        };

        auto test_data = GENERATE(
            data{win::quicktiles::left, {0, 0, 640, 1024}, 0, win::quicktiles::none},
            data{win::quicktiles::top, {0, 0, 1280, 512}, 1, win::quicktiles::top},
            data{win::quicktiles::right, {640, 0, 640, 1024}, 1, win::quicktiles::left},
            data{win::quicktiles::bottom, {0, 512, 1280, 512}, 1, win::quicktiles::bottom},
            data{win::quicktiles::left | win::quicktiles::top,
                 {0, 0, 640, 512},
                 0,
                 win::quicktiles::none},
            data{win::quicktiles::right | win::quicktiles::top,
                 {640, 0, 640, 512},
                 1,
                 win::quicktiles::left | win::quicktiles::top},
            data{win::quicktiles::left | win::quicktiles::bottom,
                 {0, 512, 640, 512},
                 0,
                 win::quicktiles::none},
            data{win::quicktiles::right | win::quicktiles::bottom,
                 {640, 512, 640, 512},
                 1,
                 win::quicktiles::left | win::quicktiles::bottom},
            data{win::quicktiles::maximize, {0, 0, 1280, 1024}, 0, win::quicktiles::none});

        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &win::space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);

        auto const origGeo = client->geo.frame;

        // now quick tile
        QSignalSpy quickTileChangedSpy(client->qobject.get(),
                                       &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());

        win::set_quicktile_mode(client, test_data.mode, true);

        QCOMPARE(client->control->quicktiling, test_data.mode);
        REQUIRE(client->geo.frame == test_data.expected_geo);
        QCOMPARE(client->geo.restore.max, origGeo);
        QCOMPARE(quickTileChangedSpy.count(), 1);

        QCOMPARE(client->topo.central_output, setup.base->outputs.at(0));

        // quick tile to same edge again should also act like send to screen
        win::set_quicktile_mode(client, test_data.mode, true);
        REQUIRE(static_cast<int>(
                    base::get_output_index(setup.base->outputs, *client->topo.central_output))
                == test_data.expected_screen);
        QCOMPARE(client->control->quicktiling, test_data.expected_mode);
        REQUIRE(client->geo.restore.max.isValid()
                == (test_data.expected_mode != win::quicktiles::none));
        REQUIRE(client->geo.restore.max
                == (test_data.expected_mode != win::quicktiles::none ? origGeo : QRect()));

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("x11 quick tiling after vert maximize")
    {
        struct data {
            win::quicktiles mode;
            QRect expected_geo;
        };

        auto test_data
            = GENERATE(data{win::quicktiles::left, {0, 0, 640, 1024}},
                       data{win::quicktiles::top, {0, 0, 1280, 512}},
                       data{win::quicktiles::right, {640, 0, 640, 1024}},
                       data{win::quicktiles::bottom, {0, 512, 1280, 512}},
                       data{win::quicktiles::left | win::quicktiles::top, {0, 0, 640, 512}},
                       data{win::quicktiles::right | win::quicktiles::top, {640, 0, 640, 512}},
                       data{win::quicktiles::left | win::quicktiles::bottom, {0, 512, 640, 512}},
                       data{win::quicktiles::right | win::quicktiles::bottom, {640, 512, 640, 512}},
                       data{win::quicktiles::maximize, {0, 0, 1280, 1024}});

        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &win::space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);

        const QRect origGeo = client->geo.frame;
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        // vertically maximize the window
        win::maximize(client, flags(client->maximizeMode() ^ win::maximize_mode::vertical));
        QCOMPARE(client->geo.frame.width(), origGeo.width());
        QCOMPARE(client->geo.size().height(), client->topo.central_output->geometry().height());
        QCOMPARE(client->geo.restore.max, origGeo);

        // now quick tile
        QSignalSpy quickTileChangedSpy(client->qobject.get(),
                                       &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());

        win::set_quicktile_mode(client, test_data.mode, true);
        QCOMPARE(client->control->quicktiling, test_data.mode);
        REQUIRE(client->geo.frame == test_data.expected_geo);
        QCOMPARE(quickTileChangedSpy.count(), 1);

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("shortcut")
    {
        using namespace Wrapland::Client;

        struct data {
            std::vector<std::string> shortcuts;
            win::quicktiles expected_mode;
            QRect expected_geo;
        };

        auto test_data = GENERATE(
            data{{"Window Quick Tile Top"}, win::quicktiles::top, {0, 0, 1280, 512}},
            data{{"Window Quick Tile Bottom"}, win::quicktiles::bottom, {0, 512, 1280, 512}},
            data{{"Window Quick Tile Top Right"},
                 win::quicktiles::top | win::quicktiles::right,
                 {640, 0, 640, 512}},
            data{{"Window Quick Tile Top Left"},
                 win::quicktiles::top | win::quicktiles::left,
                 {0, 0, 640, 512}},
            data{{"Window Quick Tile Bottom Right"},
                 win::quicktiles::bottom | win::quicktiles::right,
                 {640, 512, 640, 512}},
            data{{"Window Quick Tile Bottom Left"},
                 win::quicktiles::bottom | win::quicktiles::left,
                 {0, 512, 640, 512}},
            data{{"Window Quick Tile Left"}, win::quicktiles::left, {0, 0, 640, 1024}},
            data{{"Window Quick Tile Right"}, win::quicktiles::right, {640, 0, 640, 1024}},
            data{{"Window Quick Tile Left", "Window Quick Tile Top"},
                 win::quicktiles::top | win::quicktiles::left,
                 {0, 0, 640, 512}},
            data{{"Window Quick Tile Right", "Window Quick Tile Top"},
                 win::quicktiles::top | win::quicktiles::right,
                 {640, 0, 640, 512}},
            data{{"Window Quick Tile Left", "Window Quick Tile Bottom"},
                 win::quicktiles::bottom | win::quicktiles::left,
                 {0, 512, 640, 512}},
            data{{"Window Quick Tile Right", "Window Quick Tile Bottom"},
                 win::quicktiles::bottom | win::quicktiles::right,
                 {640, 512, 640, 512}});

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // Map the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);

        // We have to receive a configure event when the client becomes active.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        int const numberOfQuickTileActions = test_data.shortcuts.size();

        if (numberOfQuickTileActions > 1) {
            QTest::qWait(1001);
        }

        for (auto const& shortcut : test_data.shortcuts) {
            // invoke global shortcut through dbus
            auto msg
                = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                 QStringLiteral("/component/kwin"),
                                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                                 QStringLiteral("invokeShortcut"));
            msg.setArguments(QList<QVariant>{QString::fromStdString(shortcut)});
            QDBusConnection::sessionBus().asyncCall(msg);
        }

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());
        QTRY_COMPARE(quickTileChangedSpy.count(), numberOfQuickTileActions);

        // at this point the geometry did not yet change
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));

        // but quick tile mode already changed
        REQUIRE(c->control->quicktiling == test_data.expected_mode);

        // but we got requested a new geometry
        QTRY_COMPARE(configureRequestedSpy.count(), numberOfQuickTileActions + 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, test_data.expected_geo.size());

        // attach a new image
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, test_data.expected_geo.size(), Qt::red);

        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(c->geo.frame, test_data.expected_geo);
    }

    SECTION("script")
    {
        using namespace Wrapland::Client;

        struct data {
            std::string action;
            win::quicktiles expected_mode;
            QRect expected_geo;
        };

        auto test_data = GENERATE(
            data{"Top", win::quicktiles::top, {0, 0, 1280, 512}},
            data{"Bottom", win::quicktiles::bottom, {0, 512, 1280, 512}},
            data{"TopRight", win::quicktiles::top | win::quicktiles::right, {640, 0, 640, 512}},
            data{"TopLeft", win::quicktiles::top | win::quicktiles::left, {0, 0, 640, 512}},
            data{"BottomRight",
                 win::quicktiles::bottom | win::quicktiles::right,
                 {640, 512, 640, 512}},
            data{"BottomLeft", win::quicktiles::bottom | win::quicktiles::left, {0, 512, 640, 512}},
            data{"Left", win::quicktiles::left, {0, 0, 640, 1024}},
            data{"Right", win::quicktiles::right, {640, 0, 640, 1024}});

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // Map the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QCOMPARE(c->control->quicktiling, win::quicktiles::none);

        // We have to receive a configure event upon the client becoming active.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
        QVERIFY(quickTileChangedSpy.isValid());
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());

        QVERIFY(setup.base->space->scripting);
        QTemporaryFile tmpFile;
        QVERIFY(tmpFile.open());

        QTextStream out(&tmpFile);
        out << "workspace.slotWindowQuickTile" << QString::fromStdString(test_data.action) << "()";
        out.flush();

        auto const id = setup.base->space->scripting->loadScript(tmpFile.fileName());
        QVERIFY(id != -1);
        QVERIFY(setup.base->space->scripting->isScriptLoaded(tmpFile.fileName()));
        auto s = setup.base->space->scripting->findScript(tmpFile.fileName());
        QVERIFY(s);
        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        QVERIFY(runningChangedSpy.isValid());
        s->run();

        QVERIFY(quickTileChangedSpy.wait());
        QCOMPARE(quickTileChangedSpy.count(), 1);

        QCOMPARE(runningChangedSpy.count(), 1);
        QCOMPARE(runningChangedSpy.first().first().toBool(), true);

        // at this point the geometry did not yet change
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        // but quick tile mode already changed
        QCOMPARE(c->control->quicktiling, test_data.expected_mode);

        // but we got requested a new geometry
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, test_data.expected_geo.size());

        // attach a new image
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, test_data.expected_geo.size(), Qt::red);

        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(c->geo.frame, test_data.expected_geo);
    }
}

}
