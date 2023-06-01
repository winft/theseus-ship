/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/control.h"
#include "win/move.h"
#include "win/net.h"
#include "win/space.h"
#include "win/stacking_order.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("plasma surface", "[win]")
{
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("plasma-surface", operation_mode);
    setup.start();
    cursor()->set_pos(640, 512);
    setup_wayland_connection(global_selection::plasma_shell);
    auto plasma_shell = get_client().interfaces.plasma_shell.get();

    SECTION("role on all desktops")
    {
        // this test verifies that a XdgShellClient is set on all desktops when the role changes

        struct data {
            PlasmaShellSurface::Role role;
            bool expected_on_all_desktops;
        };

        auto test_data = GENERATE(data{PlasmaShellSurface::Role::Desktop, true},
                                  data{PlasmaShellSurface::Role::Panel, true},
                                  data{PlasmaShellSurface::Role::OnScreenDisplay, true},
                                  data{PlasmaShellSurface::Role::Normal, false},
                                  data{PlasmaShellSurface::Role::Notification, true},
                                  data{PlasmaShellSurface::Role::ToolTip, true},
                                  data{PlasmaShellSurface::Role::CriticalNotification, true},
                                  data{PlasmaShellSurface::Role::AppletPopup, true});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);

        // now render to map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);

        // currently the role is not yet set, so the window should not be on all desktops
        QCOMPARE(win::on_all_desktops(c), false);

        // now let's try to change that
        QSignalSpy onAllDesktopsSpy(c->qobject.get(), &win::window_qobject::desktopsChanged);
        QVERIFY(onAllDesktopsSpy.isValid());
        plasmaSurface->setRole(test_data.role);
        QCOMPARE(onAllDesktopsSpy.wait(500), test_data.expected_on_all_desktops);
        QCOMPARE(win::on_all_desktops(c), test_data.expected_on_all_desktops);

        // let's create a second window where we init a little bit different
        // first creating the PlasmaSurface then the Shell Surface
        std::unique_ptr<Surface> surface2(create_surface());
        QVERIFY(surface2);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface2(
            plasma_shell->createSurface(surface2.get()));
        QVERIFY(plasmaSurface2);
        plasmaSurface2->setRole(test_data.role);
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        QVERIFY(shellSurface2);

        auto c2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(c2);
        QVERIFY(c != c2);

        QCOMPARE(win::on_all_desktops(c2), test_data.expected_on_all_desktops);
    }

    SECTION("accepts focus")
    {
        // this test verifies that some surface roles don't get focus

        struct data {
            PlasmaShellSurface::Role role;
            bool wants_input;
            bool active;
        };

        auto test_data
            = GENERATE(data{PlasmaShellSurface::Role::Desktop, true, true},
                       data{PlasmaShellSurface::Role::Panel, true, false},
                       data{PlasmaShellSurface::Role::OnScreenDisplay, false, false},
                       data{PlasmaShellSurface::Role::Normal, true, true},
                       data{PlasmaShellSurface::Role::Notification, false, false},
                       data{PlasmaShellSurface::Role::ToolTip, false, false},
                       data{PlasmaShellSurface::Role::CriticalNotification, false, false},
                       data{PlasmaShellSurface::Role::AppletPopup, true, true});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(test_data.role);

        // now render to map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        REQUIRE(c->wantsInput() == test_data.wants_input);
        REQUIRE(c->control->active == test_data.active);
    }

    SECTION("desktop is opaque")
    {
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Desktop);

        // now render to map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(c->windowType(), win::win_type::desktop);
        QVERIFY(win::is_desktop(c));

        QVERIFY(!win::has_alpha(*c));
        QCOMPARE(c->render_data.bit_depth, 24);
    }

    SECTION("panel windows can cover")
    {
        // this test verifies the behavior of a panel with windows can cover
        // triggering the screen edge should raise the panel.

        struct data {
            QRect panel_geo;
            QRect window_geo;
            QPoint trigger_point;
        };

        auto test_data = GENERATE(data{{0, 0, 1280, 30}, {0, 0, 200, 300}, {100, 0}},
                                  data{{0, 0, 1000, 30}, {0, 0, 200, 300}, {100, 0}},
                                  data{{280, 0, 1000, 30}, {1000, 0, 200, 300}, {1000, 0}},
                                  data{{0, 994, 1280, 30}, {0, 724, 200, 300}, {100, 1023}},
                                  data{{0, 994, 1000, 30}, {0, 724, 200, 300}, {100, 1023}},
                                  data{{280, 994, 1000, 30}, {1000, 724, 200, 300}, {1000, 1023}},
                                  data{{0, 0, 30, 1024}, {0, 0, 200, 300}, {0, 100}},
                                  data{{0, 0, 30, 800}, {0, 0, 200, 300}, {0, 100}},
                                  data{{0, 200, 30, 824}, {0, 0, 200, 300}, {0, 250}},
                                  data{{1250, 0, 30, 1024}, {1080, 0, 200, 300}, {1279, 100}},
                                  data{{1250, 0, 30, 800}, {1080, 0, 200, 300}, {1279, 100}},
                                  data{{1250, 200, 30, 824}, {1080, 0, 200, 300}, {1279, 250}});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPosition(test_data.panel_geo.topLeft());
        plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::WindowsCanCover);

        // now render and map the window
        auto panel = render_and_wait_for_shown(surface, test_data.panel_geo.size(), Qt::blue);

        QVERIFY(panel);
        QCOMPARE(panel->windowType(), win::win_type::dock);
        QVERIFY(win::is_dock(panel));
        QCOMPARE(panel->geo.frame, test_data.panel_geo);
        QCOMPARE(panel->hasStrut(), false);
        QCOMPARE(win::space_window_area(*setup.base->space, win::area_option::maximize, 0, 0),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::get_layer(*panel), KWin::win::layer::normal);

        // create a Window
        std::unique_ptr<Surface> surface2(create_surface());
        QVERIFY(surface2);
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        QVERIFY(shellSurface2);

        auto c = render_and_wait_for_shown(surface2, test_data.window_geo.size(), Qt::red);
        QVERIFY(c);
        QCOMPARE(c->windowType(), win::win_type::normal);
        QVERIFY(c->control->active);
        QCOMPARE(win::get_layer(*c), KWin::win::layer::normal);
        win::move(c, test_data.window_geo.topLeft());
        QCOMPARE(c->geo.frame, test_data.window_geo);

        auto stackingOrder = setup.base->space->stacking.order.stack;
        QCOMPARE(stackingOrder.size(), 2);
        QCOMPARE(get_wayland_window(stackingOrder.front()), panel);
        QCOMPARE(get_wayland_window(stackingOrder.back()), c);

        QSignalSpy stackingOrderChangedSpy(setup.base->space->stacking.order.qobject.get(),
                                           &win::stacking_order_qobject::changed);
        QVERIFY(stackingOrderChangedSpy.isValid());

        // trigger screenedge
        cursor()->set_pos(test_data.trigger_point);
        QCOMPARE(stackingOrderChangedSpy.count(), 1);
        stackingOrder = setup.base->space->stacking.order.stack;
        QCOMPARE(stackingOrder.size(), 2);
        QCOMPARE(get_wayland_window(stackingOrder.front()), c);
        QCOMPARE(get_wayland_window(stackingOrder.back()), panel);
    }

    SECTION("osd placement")
    {
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::OnScreenDisplay);

        // now render and map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(c->windowType(), win::win_type::on_screen_display);
        QVERIFY(win::is_on_screen_display(c));
        QCOMPARE(c->geo.frame, QRect(590, 657, 100, 50));

        // change the screen size
        QSignalSpy screensChangedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(screensChangedSpy.isValid());

        auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
        setup.set_outputs(geometries);

        QCOMPARE(screensChangedSpy.count(), 1);
        test_outputs_geometries(geometries);
        QCOMPARE(c->geo.frame, QRect(590, 657, 100, 50));

        // change size of window
        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());

        render(surface, QSize(200, 100), Qt::red);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(c->geo.frame, QRect(540, 632, 200, 100));
    }

    SECTION("osd placement manual position")
    {
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);

        plasmaSurface->setRole(PlasmaShellSurface::Role::OnScreenDisplay);
        plasmaSurface->setPosition(QPoint(50, 70));

        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // now render and map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QVERIFY(c->isInitialPositionSet());
        QCOMPARE(c->windowType(), win::win_type::on_screen_display);
        QVERIFY(win::is_on_screen_display(c));
        QCOMPARE(c->geo.frame, QRect(50, 70, 100, 50));
    }

    SECTION("panel type has strut")
    {
        struct data {
            PlasmaShellSurface::PanelBehavior panel_behavior;
            bool expected_strut;
            QRect expected_max_area;
            win::layer expected_layer;
        };

        auto test_data = GENERATE(data{PlasmaShellSurface::PanelBehavior::AlwaysVisible,
                                       true,
                                       {0, 50, 1280, 974},
                                       win::layer::dock},
                                  data{PlasmaShellSurface::PanelBehavior::AutoHide,
                                       false,
                                       {0, 0, 1280, 1024},
                                       win::layer::above},
                                  data{PlasmaShellSurface::PanelBehavior::WindowsCanCover,
                                       false,
                                       {0, 0, 1280, 1024},
                                       win::layer::normal},
                                  data{PlasmaShellSurface::PanelBehavior::WindowsGoBelow,
                                       false,
                                       {0, 0, 1280, 1024},
                                       win::layer::above});

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<QObject> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPosition(QPoint(0, 0));
        plasmaSurface->setPanelBehavior(test_data.panel_behavior);

        // now render and map the window
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(c);
        QCOMPARE(c->windowType(), win::win_type::dock);
        QVERIFY(win::is_dock(c));
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
        REQUIRE(c->hasStrut() == test_data.expected_strut);
        REQUIRE(win::space_window_area(*setup.base->space, win::area_option::maximize, 0, 0)
                == test_data.expected_max_area);
        REQUIRE(win::get_layer(*c) == test_data.expected_layer);
    }

    SECTION("panel activate")
    {
        auto activate = GENERATE(true, false);

        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPanelTakesFocus(activate);

        auto panel = render_and_wait_for_shown(surface, QSize(100, 200), Qt::blue);

        QVERIFY(panel);
        QCOMPARE(panel->windowType(), win::win_type::dock);
        QVERIFY(win::is_dock(panel));
        QCOMPARE(panel->dockWantsInput(), activate);
        QCOMPARE(panel->control->active, activate);
    }

    SECTION("open under cursor")
    {
        struct data {
            QPoint cursor_pos;
            QRect expected_place;
        };

        // origin, offset-small, offset-large
        auto test_data = GENERATE(data{{}, {0, 0, 100, 50}},
                                  data{{50, 50}, {0, 25, 100, 50}},
                                  data{{500, 400}, {450, 375, 100, 50}});

        cursor()->set_pos(test_data.cursor_pos);

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        auto plasmaSurface
            = std::unique_ptr<PlasmaShellSurface>(plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->request_open_under_cursor();

        auto c = render_and_wait_for_shown(surface, test_data.expected_place.size(), Qt::blue);

        QVERIFY(c);
        QCOMPARE(c->geo.frame, test_data.expected_place);
    }

    QTRY_VERIFY(setup.base->space->stacking.order.stack.empty());
}

}
