/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
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

std::function<void(space&)> get_space_pack_method(std::string const& method_name)
{
    if (method_name == "left") {
        return win::active_window_pack_left<space>;
    } else if (method_name == "up") {
        return &win::active_window_pack_up<space>;
    } else if (method_name == "right") {
        return &win::active_window_pack_right<space>;
    } else if (method_name == "down") {
        return &win::active_window_pack_down<space>;
    }
    return {};
}

std::function<void(space&)> get_space_grow_shrink_method(std::string const& method_name)
{
    if (method_name == "grow vertical") {
        return win::active_window_grow_vertical<space>;
    } else if (method_name == "grow horizontal") {
        return win::active_window_grow_horizontal<space>;
    } else if (method_name == "shrink vertical") {
        return win::active_window_shrink_vertical<space>;
    } else if (method_name == "shrink horizontal") {
        return win::active_window_shrink_horizontal<space>;
    }
    return {};
}

}

TEST_CASE("move resize window", "[win]")
{
    test::setup setup("move-resize-window", base::operation_mode::xwayland);
    setup.start();
    test_outputs_geometries({{0, 0, 1280, 1024}});
    setup_wayland_connection(global_selection::plasma_shell | global_selection::seat);
    QVERIFY(wait_for_wayland_pointer());

    auto get_x11_window_from_id
        = [&](uint32_t id) { return get_x11_window(setup.base->mod.space->windows_map.at(id)); };

    SECTION("move")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));

        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                       &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(startMoveResizedSpy.isValid());
        QSignalSpy moveResizedChangedSpy(c->qobject.get(),
                                         &win::window_qobject::moveResizedChanged);
        QVERIFY(moveResizedChangedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(c->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            c->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QSignalSpy windowStartUserMovedResizedSpy(c->render->effect.get(),
                                                  &EffectWindow::windowStartUserMovedResized);
        QVERIFY(windowStartUserMovedResizedSpy.isValid());
        QSignalSpy windowStepUserMovedResizedSpy(c->render->effect.get(),
                                                 &EffectWindow::windowStepUserMovedResized);
        QVERIFY(windowStepUserMovedResizedSpy.isValid());
        QSignalSpy windowFinishUserMovedResizedSpy(c->render->effect.get(),
                                                   &EffectWindow::windowFinishUserMovedResized);
        QVERIFY(windowFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(c), false);

        // begin move
        win::active_window_move(*setup.base->mod.space);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), c);
        QCOMPARE(startMoveResizedSpy.count(), 1);
        QCOMPARE(moveResizedChangedSpy.count(), 1);
        QCOMPARE(windowStartUserMovedResizedSpy.count(), 1);
        QCOMPARE(win::is_move(c), true);
        QCOMPARE(c->geo.restore.max, QRect(0, 0, 100, 50));

        // send some key events, not going through input redirection
        auto const cursorPos = cursor()->pos();
        win::key_press_event(c, Qt::Key_Right);
        win::update_move_resize(c, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));

        // First event is ignored.
        REQUIRE_FALSE(clientStepUserMovedResizedSpy.count() == 1);
        clientStepUserMovedResizedSpy.clear();
        windowStepUserMovedResizedSpy.clear();

        win::key_press_event(c, Qt::Key_Right);
        win::update_move_resize(c, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(16, 0));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
        QCOMPARE(windowStepUserMovedResizedSpy.count(), 1);

        win::key_press_event(c, Qt::Key_Down | Qt::ALT);
        win::update_move_resize(c, cursor()->pos());
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);
        QCOMPARE(windowStepUserMovedResizedSpy.count(), 2);
        QCOMPARE(c->geo.frame, QRect(16, 32, 100, 50));
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(16, 32));

        // let's end
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        win::key_press_event(c, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QCOMPARE(moveResizedChangedSpy.count(), 2);
        QCOMPARE(windowFinishUserMovedResizedSpy.count(), 1);
        QCOMPARE(c->geo.frame, QRect(16, 32, 100, 50));
        QCOMPARE(win::is_move(c), false);
        QVERIFY(!setup.base->mod.space->move_resize_window);
        surface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("resize")
    {
        // a test case which manually resizes a window
        using namespace Wrapland::Client;

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(
            create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly));
        QVERIFY(shellSurface);

        // Wait for the initial configure event.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // Let's render.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        // We have to receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));
        QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));

        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                       &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(startMoveResizedSpy.isValid());
        QSignalSpy moveResizedChangedSpy(c->qobject.get(),
                                         &win::window_qobject::moveResizedChanged);
        QVERIFY(moveResizedChangedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(c->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            c->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        // begin resize
        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(c), false);
        QCOMPARE(win::is_resize(c), false);
        win::active_window_resize(*setup.base->mod.space);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), c);
        QCOMPARE(startMoveResizedSpy.count(), 1);
        QCOMPARE(moveResizedChangedSpy.count(), 1);
        QCOMPARE(win::is_resize(c), true);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));

        // Trigger a change.
        auto const cursorPos = cursor()->pos();
        win::key_press_event(c, Qt::Key_Right);
        win::update_move_resize(c, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));

        // The client should receive a configure event with the new size.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 4);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        QCOMPARE(cfgdata.size, QSize(108, 50));
        QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);

        // Now render new size.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, QSize(108, 50), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(c->geo.frame, QRect(0, 0, 108, 50));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        // Go down.
        win::key_press_event(c, Qt::Key_Down);
        win::update_move_resize(c, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 8));

        // The client should receive another configure event.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 5);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        QCOMPARE(cfgdata.size, QSize(108, 58));
        QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));

        // Now render new size.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, QSize(108, 58), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(c->geo.frame, QRect(0, 0, 108, 58));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);

        // Let's finalize the resize operation.
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        win::key_press_event(c, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QCOMPARE(moveResizedChangedSpy.count(), 2);
        QCOMPARE(win::is_resize(c), false);
        QVERIFY(!setup.base->mod.space->move_resize_window);

        // XdgShellClient currently doesn't send final configure event.
        REQUIRE_FALSE(configureRequestedSpy.wait(500));
        return;

        QCOMPARE(configureRequestedSpy.count(), 6);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // Destroy the client.
        surface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("pack to")
    {
        using namespace Wrapland::Client;

        struct data {
            std::string method_name;
            QRect expected_geo;
        };

        auto test_data = GENERATE(data{"left", {0, 487, 100, 50}},
                                  data{"up", {590, 0, 100, 50}},
                                  data{"right", {1180, 487, 100, 50}},
                                  data{"down", {590, 974, 100, 50}});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // let's render
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));

        // let's place it centered
        win::place_centered(c, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->geo.frame, QRect(590, 487, 100, 50));

        auto method_call = get_space_pack_method(test_data.method_name);
        QVERIFY(method_call);
        method_call(*setup.base->mod.space.get());

        REQUIRE(c->geo.frame == test_data.expected_geo);
        surface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("pack against client")
    {
        using namespace Wrapland::Client;

        struct data {
            std::string method_name;
            QRect expected_geo;
        };

        auto test_data = GENERATE(data{"left", {10, 487, 100, 50}},
                                  data{"up", {590, 10, 100, 50}},
                                  data{"right", {1170, 487, 100, 50}},
                                  data{"down", {590, 964, 100, 50}});

        std::unique_ptr<Surface> surface1(create_surface());
        QVERIFY(surface1);
        std::unique_ptr<Surface> surface2(create_surface());
        QVERIFY(surface2);
        std::unique_ptr<Surface> surface3(create_surface());
        QVERIFY(surface3);
        std::unique_ptr<Surface> surface4(create_surface());
        QVERIFY(surface4);

        std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
        QVERIFY(shellSurface1);
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        QVERIFY(shellSurface2);
        std::unique_ptr<XdgShellToplevel> shellSurface3(create_xdg_shell_toplevel(surface3));
        QVERIFY(shellSurface3);
        std::unique_ptr<XdgShellToplevel> shellSurface4(create_xdg_shell_toplevel(surface4));
        QVERIFY(shellSurface4);

        auto renderWindow = [&setup](std::unique_ptr<Surface> const& surface,
                                     std::function<void(space&)> const& method_call,
                                     const QRect& expectedGeometry) {
            // let's render
            auto c = render_and_wait_for_shown(surface, QSize(10, 10), Qt::blue);

            QVERIFY(c);
            QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
            QCOMPARE(c->geo.frame.size(), QSize(10, 10));
            // let's place it centered
            win::place_centered(c, QRect(0, 0, 1280, 1024));
            QCOMPARE(c->geo.frame, QRect(635, 507, 10, 10));
            method_call(*setup.base->mod.space.get());
            QCOMPARE(c->geo.frame, expectedGeometry);
        };
        renderWindow(surface1, &win::active_window_pack_left<space>, QRect(0, 507, 10, 10));
        renderWindow(surface2, &win::active_window_pack_up<space>, QRect(635, 0, 10, 10));
        renderWindow(surface3, &win::active_window_pack_right<space>, QRect(1270, 507, 10, 10));
        renderWindow(surface4, &win::active_window_pack_down<space>, QRect(635, 1014, 10, 10));

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
        // let's place it centered
        win::place_centered(c, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->geo.frame, QRect(590, 487, 100, 50));

        auto method_call = get_space_pack_method(test_data.method_name);
        QVERIFY(method_call);
        method_call(*setup.base->mod.space.get());
        REQUIRE(c->geo.frame == test_data.expected_geo);
    }

    SECTION("grow shrink")
    {
        using namespace Wrapland::Client;

        struct data {
            std::string method_name;
            QRect expected_geo;
        };

        auto test_data = GENERATE(data{"grow vertical", {590, 487, 100, 537}},
                                  data{"grow horizontal", {590, 487, 690, 50}},
                                  data{"shrink vertical", {590, 487, 100, 23}},
                                  data{"shrink horizontal", {590, 487, 40, 50}});

        // This helper surface ensures the test surface will shrink when calling the respective
        // methods.
        std::unique_ptr<Surface> surface1(create_surface());
        QVERIFY(surface1);
        std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
        QVERIFY(shellSurface1);

        auto window = render_and_wait_for_shown(surface1, QSize(650, 514), Qt::blue);
        QVERIFY(window);
        win::active_window_pack_right(*setup.base->mod.space);
        win::active_window_pack_down(*setup.base->mod.space);

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        QSignalSpy configure_spy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configure_spy.isValid());

        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);

        // Configure event due to activation.
        QVERIFY(configure_spy.wait());
        QCOMPARE(configure_spy.count(), 1);

        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());

        win::place_centered(c, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->geo.frame, QRect(590, 487, 100, 50));

        // Now according to test data grow/shrink vertically/horizontally.
        auto method_call = get_space_grow_shrink_method(test_data.method_name);
        QVERIFY(method_call);
        method_call(*setup.base->mod.space.get());

        QVERIFY(configure_spy.wait());
        QCOMPARE(configure_spy.count(), 2);

        shellSurface->ackConfigure(configure_spy.back().front().value<quint32>());
        render(surface, shellSurface->get_configure_data().size, Qt::red);

        QVERIFY(geometryChangedSpy.wait());
        REQUIRE(c->geo.frame == test_data.expected_geo);
    }

    SECTION("pointer move end")
    {
        // this test verifies that moving a window through pointer only ends if all buttons are
        // released
        using namespace Wrapland::Client;

        auto additional_button = GENERATE(BTN_RIGHT,
                                          BTN_MIDDLE,
                                          BTN_SIDE,
                                          BTN_EXTRA,
                                          BTN_FORWARD,
                                          BTN_BACK,
                                          BTN_TASK,
                                          range(BTN_TASK + 1, BTN_JOYSTICK));

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // let's render
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(c, get_wayland_window(setup.base->mod.space->stacking.active));
        QVERIFY(!win::is_move(c));

        // let's trigger the left button
        quint32 timestamp = 1;
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(!win::is_move(c));
        win::active_window_move(*setup.base->mod.space);
        QVERIFY(win::is_move(c));

        // let's press another button
        pointer_button_pressed(additional_button, timestamp++);
        QVERIFY(win::is_move(c));

        // release the left button, should still have the window moving
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(win::is_move(c));

        // but releasing the other button should now end moving
        pointer_button_released(additional_button, timestamp++);
        QVERIFY(!win::is_move(c));
        surface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("window side move")
    {
        using namespace Wrapland::Client;
        cursor()->set_pos(640, 512);
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy buttonSpy(pointer.get(), &Pointer::buttonStateChanged);
        QVERIFY(buttonSpy.isValid());

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // move pointer into center of geometry
        const QRect startGeometry = c->geo.frame;
        cursor()->set_pos(startGeometry.center());
        QVERIFY(pointerEnteredSpy.wait());
        QCOMPARE(pointerEnteredSpy.first().last().toPoint(), QPoint(49, 24));
        // simulate press
        quint32 timestamp = 1;
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(buttonSpy.wait());
        QSignalSpy moveStartSpy(c->qobject.get(),
                                &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(moveStartSpy.isValid());
        shellSurface->requestMove(get_client().interfaces.seat.get(),
                                  buttonSpy.first().first().value<quint32>());
        QVERIFY(moveStartSpy.wait());
        QCOMPARE(win::is_move(c), true);
        QVERIFY(pointerLeftSpy.wait());

        // move a bit
        QSignalSpy clientMoveStepSpy(c->qobject.get(),
                                     &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientMoveStepSpy.isValid());
        const QPoint startPoint = startGeometry.center();
        const int dragDistance = QApplication::startDragDistance();
        // Why?
        pointer_motion_absolute(startPoint + QPoint(dragDistance, dragDistance) + QPoint(6, 6),
                                timestamp++);
        QCOMPARE(clientMoveStepSpy.count(), 1);

        // and release again
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(pointerEnteredSpy.wait());
        QCOMPARE(win::is_move(c), false);
        QCOMPARE(c->geo.frame,
                 startGeometry.translated(QPoint(dragDistance, dragDistance) + QPoint(6, 6)));
        QCOMPARE(pointerEnteredSpy.last().last().toPoint(), QPoint(49, 24));
    }

    SECTION("plasma shell surface movable")
    {
        // this test verifies that certain window types from PlasmaShellSurface are not moveable or
        // resizable
        using namespace Wrapland::Client;

        struct data {
            Wrapland::Client::PlasmaShellSurface::Role role;
            bool movable;
            bool movable_across_screens;
            bool resizable;
        };

        auto test_data = GENERATE(
            data{Wrapland::Client::PlasmaShellSurface::Role::Normal, true, true, true},
            data{Wrapland::Client::PlasmaShellSurface::Role::Desktop, false, false, false},
            data{Wrapland::Client::PlasmaShellSurface::Role::Panel, false, false, false},
            data{Wrapland::Client::PlasmaShellSurface::Role::OnScreenDisplay, false, false, false});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        // and a PlasmaShellSurface
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(test_data.role);

        // let's render
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        REQUIRE(c->isMovable() == test_data.movable);
        REQUIRE(c->isMovableAcrossScreens() == test_data.movable_across_screens);
        REQUIRE(c->isResizable() == test_data.resizable);
        surface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("net move")
    {
        // this test verifies that a move request for an X11 window through NET API works
        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          0,
                          0,
                          100,
                          100,
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, 0, 0);
        xcb_icccm_size_hints_set_size(&hints, 1, 100, 100);
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        // let's set a no-border
        win::x11::net::win_info winInfo(c.get(),
                                        w,
                                        setup.base->x11_data.root_window,
                                        win::x11::net::WMWindowType,
                                        win::x11::net::Properties2());
        winInfo.setWindowType(win::win_type::override);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &win::space_qobject::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        const QRect origGeo = client->geo.frame;

        // let's move the cursor outside the window
        cursor()->set_pos(get_output(0)->geometry().center());
        QVERIFY(!origGeo.contains(cursor()->pos()));

        QSignalSpy moveStartSpy(client->qobject.get(),
                                &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(moveStartSpy.isValid());
        QSignalSpy moveEndSpy(client->qobject.get(),
                              &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(moveEndSpy.isValid());
        QSignalSpy moveStepSpy(client->qobject.get(),
                               &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(moveStepSpy.isValid());
        QVERIFY(!setup.base->mod.space->move_resize_window);

        // use NETRootInfo to trigger a move request
        win::x11::net::root_info root(c.get(), win::x11::net::Properties());
        root.moveResizeRequest(w, origGeo.center().x(), origGeo.center().y(), win::x11::net::Move);
        xcb_flush(c.get());

        QVERIFY(moveStartSpy.wait());
        QCOMPARE(get_x11_window(setup.base->mod.space->move_resize_window), client);
        QVERIFY(win::is_move(client));
        QCOMPARE(client->geo.restore.max, origGeo);
        QCOMPARE(cursor()->pos(), origGeo.center());

        // let's move a step
        cursor()->set_pos(cursor()->pos() + QPoint(10, 10));
        QCOMPARE(moveStepSpy.count(), 1);
        QCOMPARE(moveStepSpy.first().last().toRect(), origGeo.translated(10, 10));

        // let's cancel the move resize again through the net API
        root.moveResizeRequest(w,
                               client->geo.frame.center().x(),
                               client->geo.frame.center().y(),
                               win::x11::net::MoveResizeCancel);
        xcb_flush(c.get());
        QVERIFY(moveEndSpy.wait());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("adjust window geometry of autohiding x11 panel")
    {
        // this test verifies that auto hiding panels are ignored when adjusting client geometry
        // see BUG 365892

        struct data {
            QRect panel_geo;
            QPoint target_point;
            QPoint expected_adjusted_point;
            uint32_t hide_location;
        };

        // top, bottom, left, right
        auto test_data = GENERATE(
            data{{0, 0, 100, 20}, {50, 25}, {50, 20}, 0},
            data{{0, 1024 - 20, 100, 20}, {50, 1024 - 25 - 50}, {50, 1024 - 20 - 50}, 2},
            data{{0, 0, 20, 100}, {25, 50}, {20, 50}, 3},
            data{{1280 - 20, 0, 20, 100}, {1280 - 25 - 100, 50}, {1280 - 20 - 100, 50}, 1});

        // first create our panel
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          test_data.panel_geo.x(),
                          test_data.panel_geo.y(),
                          test_data.panel_geo.width(),
                          test_data.panel_geo.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(
            &hints, 1, test_data.panel_geo.x(), test_data.panel_geo.y());
        xcb_icccm_size_hints_set_size(
            &hints, 1, test_data.panel_geo.width(), test_data.panel_geo.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info winInfo(c.get(),
                                        w,
                                        setup.base->x11_data.root_window,
                                        win::x11::net::WMWindowType,
                                        win::x11::net::Properties2());
        winInfo.setWindowType(win::win_type::dock);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &win::space_qobject::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto panel = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(panel);
        QCOMPARE(panel->xcb_windows.client, w);
        TRY_REQUIRE(panel->geo.frame == test_data.panel_geo);
        QVERIFY(win::is_dock(panel));

        // let's create a window
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto testWindow = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(testWindow);
        QVERIFY(testWindow->isMovable());
        // panel is not yet hidden, we should snap against it
        REQUIRE(win::adjust_window_position(
                    *setup.base->mod.space, *testWindow, test_data.target_point, false)
                == test_data.expected_adjusted_point);

        // now let's hide the panel
        QSignalSpy panelHiddenSpy(panel->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(panelHiddenSpy.isValid());
        xcb_change_property(c.get(),
                            XCB_PROP_MODE_REPLACE,
                            w,
                            setup.base->mod.space->atoms->kde_screen_edge_show,
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            &test_data.hide_location);
        xcb_flush(c.get());
        QVERIFY(panelHiddenSpy.wait());

        // now try to snap again
        QCOMPARE(win::adjust_window_position(
                     *setup.base->mod.space, *testWindow, test_data.target_point, false),
                 test_data.target_point);

        // and destroy the panel again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy panelClosedSpy(panel->qobject.get(), &win::window_qobject::closed);
        QVERIFY(panelClosedSpy.isValid());
        QVERIFY(panelClosedSpy.wait());

        // snap once more
        REQUIRE(win::adjust_window_position(
                    *setup.base->mod.space, *testWindow, test_data.target_point, false)
                == test_data.target_point);

        // and close
        QSignalSpy windowClosedSpy(testWindow->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        shellSurface.reset();
        surface.reset();
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("adjust window geometry of autohiding wayland panel")
    {
        // this test verifies that auto hiding panels are ignored when adjusting client geometry
        // see BUG 365892
        using namespace Wrapland::Client;

        struct data {
            QRect panel_geo;
            QPoint target_point;
            QPoint expected_adjusted_point;
        };

        // top, bottom, left, right
        auto test_data
            = GENERATE(data{{0, 0, 100, 20}, {50, 25}, {50, 20}},
                       data{{0, 1024 - 20, 100, 20}, {50, 1024 - 25 - 50}, {50, 1024 - 20 - 50}},
                       data{{0, 0, 20, 100}, {25, 50}, {20, 50}},
                       data{{1280 - 20, 0, 20, 100}, {1280 - 25 - 100, 50}, {1280 - 20 - 100, 50}});

        // first create our panel
        std::unique_ptr<Surface> panelSurface(create_surface());
        QVERIFY(panelSurface);
        std::unique_ptr<XdgShellToplevel> panelShellSurface(
            create_xdg_shell_toplevel(panelSurface));
        QVERIFY(panelShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AutoHide);
        plasmaSurface->setPosition(test_data.panel_geo.topLeft());

        // let's render
        auto panel = render_and_wait_for_shown(panelSurface, test_data.panel_geo.size(), Qt::blue);
        QVERIFY(panel);
        QCOMPARE(panel->geo.frame, test_data.panel_geo);
        QVERIFY(win::is_dock(panel));

        // let's create a window
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto testWindow = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(testWindow);
        QVERIFY(testWindow->isMovable());

        // panel is not yet hidden, we should snap against it
        REQUIRE(win::adjust_window_position(
                    *setup.base->mod.space, *testWindow, test_data.target_point, false)
                == test_data.expected_adjusted_point);

        // now let's hide the panel
        QSignalSpy panelHiddenSpy(panel->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(panelHiddenSpy.isValid());
        plasmaSurface->requestHideAutoHidingPanel();
        QVERIFY(panelHiddenSpy.wait());

        // now try to snap again
        QCOMPARE(win::adjust_window_position(
                     *setup.base->mod.space, *testWindow, test_data.target_point, false),
                 test_data.target_point);

        // and destroy the panel again
        QSignalSpy panelClosedSpy(panel->qobject.get(), &win::window_qobject::closed);
        QVERIFY(panelClosedSpy.isValid());
        plasmaSurface.reset();
        panelShellSurface.reset();
        panelSurface.reset();
        QVERIFY(panelClosedSpy.wait());

        // snap once more
        QCOMPARE(win::adjust_window_position(
                     *setup.base->mod.space, *testWindow, test_data.target_point, false),
                 test_data.target_point);

        // and close
        QSignalSpy windowClosedSpy(testWindow->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        shellSurface.reset();
        surface.reset();
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("destroy move window")
    {
        // This test verifies that active move operation gets finished when
        // the associated client is destroyed.

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // Start moving the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);
        win::active_window_move(*setup.base->mod.space);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), client);
        QCOMPARE(win::is_move(client), true);
        QCOMPARE(win::is_resize(client), false);

        // Let's pretend that the client crashed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        QVERIFY(!setup.base->mod.space->move_resize_window);
    }

    SECTION("destroy resize window")
    {
        // This test verifies that active resize operation gets finished when
        // the associated client is destroyed.

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // Start resizing the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);
        win::active_window_resize(*setup.base->mod.space);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), client);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), true);

        // Let's pretend that the client crashed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        QVERIFY(!setup.base->mod.space->move_resize_window);
    }

    SECTION("unmap move window")
    {
        // This test verifies that active move operation gets cancelled when
        // the associated client is unmapped.

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // Start resizing the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);
        win::active_window_move(*setup.base->mod.space);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), client);
        QCOMPARE(win::is_move(client), true);
        QCOMPARE(win::is_resize(client), false);

        // Unmap the client while we're moving it.
        QSignalSpy hiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(hiddenSpy.isValid());
        surface->attachBuffer(Buffer::Ptr());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(hiddenSpy.wait());
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);

        // Destroy the client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(client));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    }

    SECTION("unmap resize window")
    {
        // This test verifies that active resize operation gets cancelled when
        // the associated client is unmapped.

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // Start resizing the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);
        win::active_window_resize(*setup.base->mod.space);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QCOMPARE(get_wayland_window(setup.base->mod.space->move_resize_window), client);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), true);

        // Unmap the client while we're resizing it.
        QSignalSpy hiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(hiddenSpy.isValid());
        surface->attachBuffer(Buffer::Ptr());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(hiddenSpy.wait());
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(win::is_move(client), false);
        QCOMPARE(win::is_resize(client), false);

        // Destroy the client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(client));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    }

    SECTION("set fullscreen while moving")
    {
        // Ensure we disable moving event when setFullScreen is triggered
        using namespace Wrapland::Client;

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(500, 800), Qt::blue);
        QVERIFY(client);

        QSignalSpy fullscreen_spy(client->qobject.get(), &win::window_qobject::fullScreenChanged);
        QVERIFY(fullscreen_spy.isValid());
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        QVERIFY(configureRequestedSpy.isValid());
        QVERIFY(configureRequestedSpy.wait());

        win::active_window_move(*setup.base->mod.space);
        QCOMPARE(win::is_move(client), true);

        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        auto cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::fullscreen));

        QCOMPARE(cfgdata.size, QSize(500, 800));

        client->setFullScreen(true);

        QCOMPARE(client->control->fullscreen, false);

        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::fullscreen));
        QCOMPARE(cfgdata.size, get_output(0)->geometry().size());

        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, cfgdata.size, Qt::red);

        QVERIFY(fullscreen_spy.wait());
        QCOMPARE(fullscreen_spy.size(), 1);

        QCOMPARE(client->control->fullscreen, true);
        QCOMPARE(win::is_move(client), false);
        QVERIFY(!setup.base->mod.space->move_resize_window);

        // Let's pretend that the client crashed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("set maximize while moving")
    {
        // Ensure we disable moving event when changeMaximize is triggered
        using namespace Wrapland::Client;

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // let's render
        auto client = render_and_wait_for_shown(surface, QSize(500, 800), Qt::blue);
        QVERIFY(client);

        win::active_window_move(*setup.base->mod.space);
        QCOMPARE(win::is_move(client), true);
        win::set_maximize(client, true, true);

        // TODO(romangg): The client is still in move state at this point. Is this correct?
        REQUIRE_FALSE(!win::is_move(client));
        REQUIRE_FALSE(!setup.base->mod.space->move_resize_window);

        // Let's pretend that the client crashed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }
}

}
