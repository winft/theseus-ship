/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KDecoration2/Decoration>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>
#include <map>
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

}

TEST_CASE("struts", "[win]")
{
    test::setup setup("struts", base::operation_mode::xwayland);

    // set custom config which disables the Outline
    auto group = setup.base->config.main->group("Outline");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::plasma_shell);
    cursor()->set_pos(QPoint(640, 512));

    auto plasma_shell = get_client().interfaces.plasma_shell.get();
    auto get_x11_window_from_id
        = [&](uint32_t id) { return get_x11_window(setup.base->mod.space->windows_map.at(id)); };

    SECTION("wayland struts")
    {
        struct data {
            std::vector<QRect> window_geos;
            std::array<QRect, 2> maximized_screen_geos;
            QRect work_area;
            QRegion restricted_move_area;
        };

        auto test_data = GENERATE(
            // bottom/0
            data{{QRect(0, 992, 1280, 32)},
                 {QRect(0, 0, 1280, 992), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 992},
                 {0, 992, 1280, 32}},
            // bottom/1
            data{{QRect(1280, 992, 1280, 32)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 992)},
                 {0, 0, 2560, 992},
                 {1280, 992, 1280, 32}},
            // top/0
            data{{QRect(0, 0, 1280, 32)},
                 {QRect(0, 32, 1280, 992), QRect(1280, 0, 1280, 1024)},
                 {0, 32, 2560, 992},
                 {0, 0, 1280, 32}},
            // top/1
            data{{QRect(1280, 0, 1280, 32)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 32, 1280, 992)},
                 {0, 32, 2560, 992},
                 {1280, 0, 1280, 32}},
            // left/0
            data{{QRect(0, 0, 32, 1024)},
                 {QRect(32, 0, 1248, 1024), QRect(1280, 0, 1280, 1024)},
                 {32, 0, 2528, 1024},
                 {0, 0, 32, 1024}},
            // left/1
            data{{QRect(1280, 0, 32, 1024)},
                 {QRect(0, 0, 1280, 1024), QRect(1312, 0, 1248, 1024)},
                 {0, 0, 2560, 1024},
                 {1280, 0, 32, 1024}},
            // right/0
            data{{QRect(1248, 0, 32, 1024)},
                 {QRect(0, 0, 1248, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {1248, 0, 32, 1024}},
            // right/1
            data{{QRect(2528, 0, 32, 1024)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1248, 1024)},
                 {0, 0, 2528, 1024},
                 {2528, 0, 32, 1024}},
            // same with partial panels not covering the whole area
            // bottom/0
            data{{QRect(100, 992, 1080, 32)},
                 {QRect(0, 0, 1280, 992), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 992},
                 {100, 992, 1080, 32}},
            // bottom/1
            data{{QRect(1380, 992, 1080, 32)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 992)},
                 {0, 0, 2560, 992},
                 {1380, 992, 1080, 32}},
            // top/0
            data{{QRect(100, 0, 1080, 32)},
                 {QRect(0, 32, 1280, 992), QRect(1280, 0, 1280, 1024)},
                 {0, 32, 2560, 992},
                 {100, 0, 1080, 32}},
            // top/1
            data{{QRect(1380, 0, 1080, 32)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 32, 1280, 992)},
                 {0, 32, 2560, 992},
                 {1380, 0, 1080, 32}},
            // left/0
            data{{QRect(0, 100, 32, 824)},
                 {QRect(32, 0, 1248, 1024), QRect(1280, 0, 1280, 1024)},
                 {32, 0, 2528, 1024},
                 {0, 100, 32, 824}},
            // left/1
            data{{QRect(1280, 100, 32, 824)},
                 {QRect(0, 0, 1280, 1024), QRect(1312, 0, 1248, 1024)},
                 {0, 0, 2560, 1024},
                 {1280, 100, 32, 824}},
            // right/0
            data{{QRect(1248, 100, 32, 824)},
                 {QRect(0, 0, 1248, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {1248, 100, 32, 824}},
            // right/1
            data{{QRect(2528, 100, 32, 824)},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1248, 1024)},
                 {0, 0, 2528, 1024},
                 {2528, 100, 32, 824}},
            // multiple panels
            // two bottom panels
            data{{QRect(100, 992, 1080, 32), QRect(1380, 984, 1080, 40)},
                 {QRect(0, 0, 1280, 992), QRect(1280, 0, 1280, 984)},
                 {0, 0, 2560, 984},
                 QRegion(100, 992, 1080, 32).united(QRegion(1380, 984, 1080, 40))},
            // two top panels
            data{{QRect(0, 10, 32, 390), QRect(0, 450, 40, 100)},
                 {QRect(40, 0, 1240, 1024), QRect(1280, 0, 1280, 1024)},
                 {40, 0, 2520, 1024},
                 QRegion(0, 10, 32, 390).united(QRegion(0, 450, 40, 100))});

        // this test verifies that struts on Wayland panels are handled correctly
        using namespace Wrapland::Client;

        auto const& outputs = setup.base->outputs;
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::movement, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize_full, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::fullscreen, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::screen, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));

        // second screen
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::movement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize_full, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::fullscreen, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::screen, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));

        // combined
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == QRect(0, 0, 2560, 1024));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::full, outputs.at(0), 1)
            == QRect(0, 0, 2560, 1024));
        REQUIRE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all)
                == QRegion());

        struct client_holder {
            wayland_window* window;
            std::unique_ptr<Wrapland::Client::PlasmaShellSurface> plasma_surface;
            std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
            std::unique_ptr<Wrapland::Client::Surface> surface;
        };

        // create the panels
        std::vector<client_holder> clients;
        for (auto const& window_geo : test_data.window_geos) {
            auto surface = create_surface();
            auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
            auto plasmaSurface = std::unique_ptr<Wrapland::Client::PlasmaShellSurface>(
                plasma_shell->createSurface(surface.get()));
            plasmaSurface->setPosition(window_geo.topLeft());
            plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
            init_xdg_shell_toplevel(surface, shellSurface);

            // map the window
            auto c = render_and_wait_for_shown(
                surface, window_geo.size(), Qt::red, QImage::Format_RGB32);

            QVERIFY(c);
            QVERIFY(!c->control->active);
            QCOMPARE(c->geo.frame, window_geo);
            QVERIFY(win::is_dock(c));
            QVERIFY(c->hasStrut());
            clients.push_back(
                {c, std::move(plasmaSurface), std::move(shellSurface), std::move(surface)});
        }

        // some props are independent of struts - those first
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::movement, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize_full, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::fullscreen, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::screen, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));

        // screen 1
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::movement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize_full, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::fullscreen, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::screen, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));

        // combined
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::full, outputs.at(0), 1)
            == QRect(0, 0, 2560, 1024));

        // now verify the actual updated client areas
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == test_data.maximized_screen_geos.at(0));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == test_data.maximized_screen_geos.at(0));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == test_data.maximized_screen_geos.at(1));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == test_data.maximized_screen_geos.at(1));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == test_data.work_area);
        REQUIRE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all)
                == test_data.restricted_move_area);

        // delete all surfaces
        for (auto& client : clients) {
            QSignalSpy destroyedSpy(client.window->qobject.get(), &QObject::destroyed);
            QVERIFY(destroyedSpy.isValid());
            client = {};
            QVERIFY(destroyedSpy.wait());
        }
        REQUIRE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all)
                == QRegion());
    }

    SECTION("move wayland panel")
    {
        // this test verifies that repositioning a Wayland panel updates the client area
        using namespace Wrapland::Client;
        const QRect windowGeometry(0, 1000, 1280, 24);
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        plasmaSurface->setPosition(windowGeometry.topLeft());
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        init_xdg_shell_toplevel(surface, shellSurface);

        // map the window
        auto c = render_and_wait_for_shown(
            surface, windowGeometry.size(), Qt::red, QImage::Format_RGB32);
        QVERIFY(c);
        QVERIFY(!c->control->active);
        REQUIRE(c->geo.frame == windowGeometry);
        QVERIFY(win::is_dock(c));
        QVERIFY(c->hasStrut());

        auto const& outputs = setup.base->outputs;
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1000));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1000));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == QRect(0, 0, 2560, 1000));

        QSignalSpy geometryChangedSpy(c->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        plasmaSurface->setPosition(QPoint(1280, 1000));
        QVERIFY(geometryChangedSpy.wait());
        REQUIRE(c->geo.frame == QRect(1280, 1000, 1280, 24));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == QRect(0, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1000));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1000));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == QRect(0, 0, 2560, 1000));
    }

    SECTION("wayland mobile panel")
    {
        using namespace Wrapland::Client;

        // First enable maxmizing policy
        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("Placement", "maximizing");
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);

        // create first top panel
        const QRect windowGeometry(0, 0, 1280, 60);
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            plasma_shell->createSurface(surface.get()));
        plasmaSurface->setPosition(windowGeometry.topLeft());
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        init_xdg_shell_toplevel(surface, shellSurface);

        // map the first panel
        auto c = render_and_wait_for_shown(
            surface, windowGeometry.size(), Qt::red, QImage::Format_RGB32);
        QVERIFY(c);
        QVERIFY(!c->control->active);
        REQUIRE(c->geo.frame == windowGeometry);
        QVERIFY(win::is_dock(c));
        QVERIFY(c->hasStrut());

        auto const& outputs = setup.base->outputs;
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == QRect(0, 60, 1280, 964));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == QRect(0, 60, 1280, 964));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == QRect(1280, 0, 1280, 1024));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == QRect(0, 60, 2560, 964));

        // create another bottom panel
        const QRect windowGeometry2(0, 874, 1280, 150);
        std::unique_ptr<Surface> surface2(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(
            create_xdg_shell_toplevel(surface2, CreationSetup::CreateOnly));
        std::unique_ptr<PlasmaShellSurface> plasmaSurface2(
            plasma_shell->createSurface(surface2.get()));
        plasmaSurface2->setPosition(windowGeometry2.topLeft());
        plasmaSurface2->setRole(PlasmaShellSurface::Role::Panel);
        init_xdg_shell_toplevel(surface2, shellSurface2);

        auto c1 = render_and_wait_for_shown(
            surface2, windowGeometry2.size(), Qt::blue, QImage::Format_RGB32);

        QVERIFY(c1);
        QVERIFY(!c1->control->active);
        QCOMPARE(c1->geo.frame, windowGeometry2);
        QVERIFY(win::is_dock(c1));
        QVERIFY(c1->hasStrut());

        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 QRect(0, 60, 1280, 814));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 QRect(0, 60, 1280, 814));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 60, 2560, 814));

        // Destroy test clients.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
        shellSurface2.reset();
        QVERIFY(wait_for_destroyed(c1));
    }

    SECTION("x11 struts")
    {
        // this test verifies that struts are applied correctly for X11 windows

        struct data {
            QRect window_geo;
            win::x11::net::extended_strut strut;
            std::array<QRect, 2> maximized_screen_geos;
            QRect work_area;
            QRegion restricted_move_area;
        };

        auto test_data = GENERATE(
            // bottom panel/no strut
            data{{0, 980, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // bottom panel/strut
            data{{0, 980, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 44,
                  .bottom_start = 0,
                  .bottom_end = 1279},
                 {QRect(0, 0, 1280, 980), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 980},
                 {0, 980, 1279, 44}},
            // top panel/no strut
            data{{0, 0, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // top panel/strut
            data{{0, 0, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 44,
                  .top_start = 0,
                  .top_end = 1279,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 44, 1280, 980), QRect(1280, 0, 1280, 1024)},
                 {0, 44, 2560, 980},
                 {0, 0, 1279, 44}},
            // left panel/no strut
            data{{0, 0, 60, 1024},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // left panel/strut
            data{{0, 0, 60, 1024},
                 {.left_width = 60,
                  .left_start = 0,
                  .left_end = 1023,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(60, 0, 1220, 1024), QRect(1280, 0, 1280, 1024)},
                 {60, 0, 2500, 1024},
                 {0, 0, 60, 1023}},
            // right panel/no strut
            data{{1220, 0, 60, 1024},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // right panel/strut
            data{{1220, 0, 60, 1024},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 1340,
                  .right_start = 0,
                  .right_end = 1023,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1220, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {1220, 0, 60, 1023}},
            // second screen
            // bottom panel 1/no strut
            data{{1280, 980, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // bottom panel 1/strut
            data{{1280, 980, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 44,
                  .bottom_start = 1280,
                  .bottom_end = 2559},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 980)},
                 {0, 0, 2560, 980},
                 {1280, 980, 1279, 44}},
            // top panel 1/no strut
            data{{1280, 0, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // top panel 1 /strut
            data{{1280, 0, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 44,
                  .top_start = 1280,
                  .top_end = 2559,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 44, 1280, 980)},
                 {0, 44, 2560, 980},
                 {1280, 0, 1279, 44}},
            // left panel 1/no strut
            data{{1280, 0, 60, 1024},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {}},
            // left panel 1/strut
            data{{1280, 0, 60, 1024},
                 {.left_width = 1340,
                  .left_start = 0,
                  .left_end = 1023,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1340, 0, 1220, 1024)},
                 {0, 0, 2560, 1024},
                 {1280, 0, 60, 1023}},
            // invalid struts
            // bottom panel/ invalid strut
            data{{0, 980, 1280, 44},
                 {.left_width = 1280,
                  .left_start = 980,
                  .left_end = 1024,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 0,
                  .top_start = 0,
                  .top_end = 0,
                  .bottom_width = 44,
                  .bottom_start = 0,
                  .bottom_end = 1279},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {0, 980, 1280, 44}},
            // top panel/ invalid strut
            data{{0, 0, 1280, 44},
                 {.left_width = 1280,
                  .left_start = 0,
                  .left_end = 44,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 44,
                  .top_start = 0,
                  .top_end = 1279,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {0, 0, 1280, 44}},
            // top panel/invalid strut 2
            data{{0, 0, 1280, 44},
                 {.left_width = 0,
                  .left_start = 0,
                  .left_end = 0,
                  .right_width = 0,
                  .right_start = 0,
                  .right_end = 0,
                  .top_width = 1024,
                  .top_start = 0,
                  .top_end = 1279,
                  .bottom_width = 0,
                  .bottom_start = 0,
                  .bottom_end = 0},
                 {QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)},
                 {0, 0, 2560, 1024},
                 {0, 0, 1279, 1024}});

        // no, struts yet
        auto const& outputs = setup.base->outputs;
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));

        // second screen
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));

        // combined
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 0, 2560, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::full, outputs.at(0), 1),
                 QRect(0, 0, 2560, 1024));
        QCOMPARE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all),
                 QRegion());

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          test_data.window_geo.x(),
                          test_data.window_geo.y(),
                          test_data.window_geo.width(),
                          test_data.window_geo.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(
            &hints, 1, test_data.window_geo.x(), test_data.window_geo.y());
        xcb_icccm_size_hints_set_size(
            &hints, 1, test_data.window_geo.width(), test_data.window_geo.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);

        info.setExtendedStrut(test_data.strut);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(!win::decoration(client));
        QCOMPARE(client->windowType(), win::win_type::dock);
        QCOMPARE(client->geo.frame, test_data.window_geo);

        // this should have affected the client area
        // some props are independent of struts - those first
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));

        // screen 1
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));

        // combined
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::full, outputs.at(0), 1),
                 QRect(0, 0, 2560, 1024));

        // now verify the actual updated client areas
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1)
                == test_data.maximized_screen_geos.at(0));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1)
                == test_data.maximized_screen_geos.at(0));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1)
                == test_data.maximized_screen_geos.at(1));
        REQUIRE(win::space_window_area(
                    *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1)
                == test_data.maximized_screen_geos.at(1));
        REQUIRE(
            win::space_window_area(*setup.base->mod.space, win::area_option::work, outputs.at(0), 1)
            == test_data.work_area);
        REQUIRE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all)
                == test_data.restricted_move_area);

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());

        // now struts should be removed again
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(0), 1),
                 QRect(0, 0, 1280, 1024));

        // second screen
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::movement, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize_full, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::fullscreen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::screen, outputs.at(1), 1),
                 QRect(1280, 0, 1280, 1024));

        // combined
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 0, 2560, 1024));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::full, outputs.at(0), 1),
                 QRect(0, 0, 2560, 1024));
        QCOMPARE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all),
                 QRegion());
    }

    SECTION("bug 363804")
    {
        // this test verifies the condition described in BUG 363804
        // two screens in a vertical setup, aligned to right border with panel on the bottom screen
        auto const geometries = std::vector<QRect>{{0, 0, 1920, 1080}, {554, 1080, 1366, 768}};
        setup.set_outputs(geometries);
        QCOMPARE(get_output(0)->geometry(), geometries.at(0));
        QCOMPARE(get_output(1)->geometry(), geometries.at(1));
        QCOMPARE(setup.base->topology.size, QSize(1920, 1848));

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry(554, 1812, 1366, 36);
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

        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);

        win::x11::net::extended_strut strut;
        strut.left_start = 0;
        strut.left_end = 0;
        strut.left_width = 0;
        strut.right_start = 0;
        strut.right_end = 0;
        strut.right_width = 0;
        strut.top_start = 0;
        strut.top_end = 0;
        strut.top_width = 0;
        strut.bottom_start = 554;
        strut.bottom_end = 1919;
        strut.bottom_width = 36;
        info.setExtendedStrut(strut);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(!win::decoration(client));
        QCOMPARE(client->windowType(), win::win_type::dock);
        QCOMPARE(client->geo.frame, windowGeometry);

        // now verify the actual updated client areas
        auto const& outputs = setup.base->outputs;
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 geometries.at(0));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 geometries.at(0));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 QRect(554, 1080, 1366, 732));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 QRect(554, 1080, 1366, 732));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 0, 1920, 1812));

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("left screen smaller bottom aligned")
    {
        // this test verifies a two screen setup with the left screen smaller than the right and
        // bottom aligned the panel is on the top of the left screen, thus not at 0/0 what this test
        // in addition tests is whether a window larger than the left screen is not placed into the
        // dead area
        auto const geometries = std::vector<QRect>{{0, 282, 1366, 768}, {1366, 0, 1680, 1050}};
        setup.set_outputs(geometries);
        QCOMPARE(get_output(0)->geometry(), geometries.at(0));
        QCOMPARE(get_output(1)->geometry(), geometries.at(1));
        QCOMPARE(setup.base->topology.size, QSize(3046, 1050));

        // create the panel
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry(0, 282, 1366, 24);
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

        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);

        win::x11::net::extended_strut strut;
        strut.left_start = 0;
        strut.left_end = 0;
        strut.left_width = 0;
        strut.right_start = 0;
        strut.right_end = 0;
        strut.right_width = 0;
        strut.top_start = 0;
        strut.top_end = 1365;
        strut.top_width = 306;
        strut.bottom_start = 0;
        strut.bottom_end = 0;
        strut.bottom_width = 0;
        info.setExtendedStrut(strut);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(!win::decoration(client));
        QCOMPARE(client->windowType(), win::win_type::dock);
        QCOMPARE(client->geo.frame, windowGeometry);

        // now verify the actual updated client areas
        auto const& outputs = setup.base->outputs;
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 QRect(0, 306, 1366, 744));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 QRect(0, 306, 1366, 744));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 geometries.at(1));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 geometries.at(1));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 0, 3046, 1050));

        // now create a window which is larger than screen 0

        xcb_window_t w2 = xcb_generate_id(c.get());
        const QRect windowGeometry2(0, 26, 1366, 2000);
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w2,
                          setup.base->x11_data.root_window,
                          windowGeometry2.x(),
                          windowGeometry2.y(),
                          windowGeometry2.width(),
                          windowGeometry2.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints2;
        memset(&hints2, 0, sizeof(hints2));
        xcb_icccm_size_hints_set_min_size(&hints2, 868, 431);
        xcb_icccm_set_wm_normal_hints(c.get(), w2, &hints2);
        xcb_map_window(c.get(), w2);
        xcb_flush(c.get());

        QVERIFY(windowCreatedSpy.wait());

        auto client2 = get_x11_window_from_id(windowCreatedSpy.last().first().value<quint32>());
        QVERIFY(client2);
        QVERIFY(client2 != client);
        QVERIFY(win::decoration(client2));

        QCOMPARE(client2->geo.frame, QRect(0, 306, 1366, 744));
        QCOMPARE(client2->maximizeMode(), win::maximize_mode::full);

        // destroy window again
        QSignalSpy normalWindowClosedSpy(client2->qobject.get(), &win::window_qobject::closed);
        QVERIFY(normalWindowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w2);
        xcb_destroy_window(c.get(), w2);
        xcb_flush(c.get());
        QVERIFY(normalWindowClosedSpy.wait());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("window move with panel between screens")
    {
        // this test verifies the condition of BUG
        // when moving a window with decorations in a restricted way it should pass from one screen
        // to the other even if there is a panel in between.

        // left screen must be smaller than right screen
        auto const geometries = std::vector<QRect>{{0, 282, 1366, 768}, {1366, 0, 1680, 1050}};
        setup.set_outputs(geometries);
        QCOMPARE(get_output(0)->geometry(), geometries.at(0));
        QCOMPARE(get_output(1)->geometry(), geometries.at(1));
        QCOMPARE(setup.base->topology.size, QSize(3046, 1050));

        // create the panel on the right screen, left edge
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry(1366, 0, 24, 1050);
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

        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);

        win::x11::net::extended_strut strut;
        strut.left_start = 0;
        strut.left_end = 1050;
        strut.left_width = 1366 + 24;
        strut.right_start = 0;
        strut.right_end = 0;
        strut.right_width = 0;
        strut.top_start = 0;
        strut.top_end = 0;
        strut.top_width = 0;
        strut.bottom_start = 0;
        strut.bottom_end = 0;
        strut.bottom_width = 0;
        info.setExtendedStrut(strut);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(!win::decoration(client));
        QCOMPARE(client->windowType(), win::win_type::dock);
        QCOMPARE(client->geo.frame, windowGeometry);

        // now verify the actual updated client areas
        auto const& outputs = setup.base->outputs;
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(0), 1),
                 QRect(0, 282, 1366, 768));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(0), 1),
                 QRect(0, 282, 1366, 768));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::placement, outputs.at(1), 1),
                 QRect(1390, 0, 1656, 1050));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::maximize, outputs.at(1), 1),
                 QRect(1390, 0, 1656, 1050));
        QCOMPARE(win::space_window_area(
                     *setup.base->mod.space, win::area_option::work, outputs.at(0), 1),
                 QRect(0, 0, 3046, 1050));
        QCOMPARE(win::restricted_move_area(*setup.base->mod.space, -1, win::strut_area::all),
                 QRegion(1366, 0, 24, 1050));

        // create another window and try to move it
        xcb_window_t w2 = xcb_generate_id(c.get());
        const QRect windowGeometry2(1500, 400, 200, 300);
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w2,
                          setup.base->x11_data.root_window,
                          windowGeometry2.x(),
                          windowGeometry2.y(),
                          windowGeometry2.width(),
                          windowGeometry2.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints2;
        memset(&hints2, 0, sizeof(hints2));
        xcb_icccm_size_hints_set_position(&hints2, 1, windowGeometry2.x(), windowGeometry2.y());
        xcb_icccm_size_hints_set_min_size(&hints2, 200, 300);
        xcb_icccm_set_wm_normal_hints(c.get(), w2, &hints2);
        xcb_map_window(c.get(), w2);
        xcb_flush(c.get());
        QVERIFY(windowCreatedSpy.wait());

        auto client2 = get_x11_window_from_id(windowCreatedSpy.last().first().value<quint32>());
        QVERIFY(client2);
        QVERIFY(client2 != client);
        QVERIFY(win::decoration(client2));
        QCOMPARE(win::frame_to_client_size(client2, client2->geo.size()), QSize(200, 300));
        QCOMPARE(client2->geo.pos(),
                 QPoint(1500, 400) - QPoint(win::left_border(client2), win::top_border(client2)));

        const QRect origGeo = client2->geo.frame;
        cursor()->set_pos(origGeo.center());
        win::perform_window_operation(client2, win::win_op::move);

        QTRY_COMPARE(get_x11_window(setup.base->mod.space->move_resize_window), client2);
        QVERIFY(win::is_move(client2));

        // move to next screen - step is 8 pixel, so 800 pixel
        for (int i = 0; i < 100; i++) {
            win::key_press_event(client2, Qt::Key_Left);
            QTest::qWait(10);
        }

        win::key_press_event(client2, Qt::Key_Enter);
        QCOMPARE(win::is_move(client2), false);
        QVERIFY(!setup.base->mod.space->move_resize_window);
        QCOMPARE(client2->geo.frame, QRect(origGeo.translated(-800, 0)));

        // Destroy window again.
        QSignalSpy normalWindowClosedSpy(client2->qobject.get(), &win::window_qobject::closed);
        QVERIFY(normalWindowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w2);
        xcb_destroy_window(c.get(), w2);
        xcb_flush(c.get());
        QVERIFY(normalWindowClosedSpy.wait());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        c.reset();

        QVERIFY(windowClosedSpy.wait());
    }
}

}
