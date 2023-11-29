/*
SPDX-FileCopyrightText: 2019 David Edmundson <davidedmundson@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

struct PlaceWindowResult {
    QSize initiallyConfiguredSize;
    Wrapland::Client::xdg_shell_states initiallyConfiguredStates;
    QRect finalGeometry;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
    std::unique_ptr<Wrapland::Client::Surface> surface;
};

const char* policy_to_string(win::placement policy)
{
    char const* const policies[] = {"NoPlacement",
                                    "Default",
                                    "XXX should never see",
                                    "Random",
                                    "Smart",
                                    "Centered",
                                    "ZeroCornered",
                                    "UnderMouse",
                                    "OnMainWindow",
                                    "Maximizing"};
    auto policy_int = static_cast<int>(policy);
    assert(policy_int < int(sizeof(policies) / sizeof(policies[0])));
    return policies[policy_int];
}

TEST_CASE("placement", "[win]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("placement", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::xdg_decoration | global_selection::plasma_shell);
    cursor()->set_pos(QPoint(512, 512));

    auto setPlacementPolicy = [&](win::placement policy) {
        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("Placement", policy_to_string(policy));
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);
    };

    auto createAndPlaceWindow = [&](QSize const& defaultSize) {
        PlaceWindowResult rc;

        QSignalSpy window_spy(setup.base->mod.space->qobject.get(),
                              &space::qobject_t::wayland_window_added);
        assert(window_spy.isValid());

        // create a new window
        rc.surface = create_surface();
        rc.toplevel = create_xdg_shell_toplevel(rc.surface, CreationSetup::CreateOnly);
        QSignalSpy configSpy(rc.toplevel.get(), &XdgShellToplevel::configured);
        assert(configSpy.isValid());

        rc.surface->commit(Surface::CommitFlag::None);
        configSpy.wait();

        auto cfgdata = rc.toplevel->get_configure_data();
        auto first_size = cfgdata.size;

        rc.toplevel->ackConfigure(configSpy.front().front().toUInt());
        configSpy.clear();

        render(rc.surface, first_size.isEmpty() ? defaultSize : first_size, Qt::red);
        configSpy.wait();
        cfgdata = rc.toplevel->get_configure_data();

        auto window_id = window_spy.first().first().value<quint32>();
        auto window = get_wayland_window(setup.base->mod.space->windows_map.at(window_id));

        assert(first_size.isEmpty() || first_size == cfgdata.size);
        rc.initiallyConfiguredSize = cfgdata.size;
        rc.initiallyConfiguredStates = cfgdata.states;
        rc.toplevel->ackConfigure(configSpy.front().front().toUInt());

        render(rc.surface, rc.initiallyConfiguredSize, Qt::red);
        configSpy.wait(100);

        rc.finalGeometry = window->geo.frame;
        return rc;
    };

    SECTION("place smart")
    {
        setPlacementPolicy(win::placement::smart);

        QRegion usedArea;

        std::vector<PlaceWindowResult> placements;
        for (int i = 0; i < 4; i++) {
            placements.push_back(createAndPlaceWindow(QSize(600, 500)));
            auto const& placement = placements.back();
            // smart placement shouldn't define a size on clients
            QCOMPARE(placement.initiallyConfiguredSize, QSize(600, 500));
            QCOMPARE(placement.finalGeometry.size(), QSize(600, 500));

            // exact placement isn't a defined concept that should be tested
            // but the goal of smart placement is to make sure windows don't overlap until they need
            // to 4 windows of 600, 500 should fit without overlap
            QVERIFY(!usedArea.intersects(placement.finalGeometry));
            usedArea += placement.finalGeometry;
        }
    }

    SECTION("place zero cornered")
    {
        setPlacementPolicy(win::placement::zero_cornered);

        std::vector<PlaceWindowResult> placements;
        for (int i = 0; i < 4; i++) {
            placements.push_back(createAndPlaceWindow(QSize(600, 500)));
            auto const& placement = placements.back();
            // smart placement shouldn't define a size on clients
            QCOMPARE(placement.initiallyConfiguredSize, QSize(600, 500));
            // size should match our buffer
            QCOMPARE(placement.finalGeometry.size(), QSize(600, 500));
            // and it should be in the corner
            QCOMPARE(placement.finalGeometry.topLeft(), QPoint(0, 0));
        }
    }

    SECTION("place maximized")
    {
        setPlacementPolicy(win::placement::maximizing);

        // add a top panel
        std::unique_ptr<Surface> panelSurface(create_surface());
        std::unique_ptr<QObject> panelShellSurface(create_xdg_shell_toplevel(panelSurface));
        QVERIFY(panelSurface);
        QVERIFY(panelShellSurface);

        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPosition(QPoint(0, 0));
        render_and_wait_for_shown(panelSurface, QSize(1280, 20), Qt::blue);

        // all windows should be initially maximized with an initial configure size sent
        std::vector<PlaceWindowResult> placements;
        for (int i = 0; i < 4; i++) {
            placements.push_back(createAndPlaceWindow(QSize(600, 500)));
            auto const& placement = placements.back();
            QVERIFY(placement.initiallyConfiguredStates & xdg_shell_state::maximized);
            QCOMPARE(placement.initiallyConfiguredSize, QSize(1280, 1024 - 20));

            // under the panel
            TRY_REQUIRE(placement.finalGeometry == QRect(0, 20, 1280, 1024 - 20));
        }
    }

    SECTION("place maximized leaves fullscreen")
    {
        setPlacementPolicy(win::placement::maximizing);

        // add a top panel
        std::unique_ptr<Surface> panelSurface(create_surface());
        std::unique_ptr<QObject> panelShellSurface(create_xdg_shell_toplevel(panelSurface));
        QVERIFY(panelSurface);
        QVERIFY(panelShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPosition(QPoint(0, 0));
        render_and_wait_for_shown(panelSurface, QSize(1280, 20), Qt::blue);

        // all windows should be initially fullscreen with an initial configure size sent, despite
        // the policy
        for (int i = 0; i < 4; i++) {
            auto surface = create_surface();
            auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
            shellSurface->setFullscreen(true);

            QSignalSpy configSpy(shellSurface.get(), &XdgShellToplevel::configured);
            surface->commit(Surface::CommitFlag::None);
            configSpy.wait();

            auto cfgdata = shellSurface->get_configure_data();
            auto initiallyConfiguredSize = cfgdata.size;
            auto initiallyConfiguredStates = cfgdata.states;
            shellSurface->ackConfigure(configSpy.front().front().toUInt());

            auto c = render_and_wait_for_shown(surface, initiallyConfiguredSize, Qt::red);

            QVERIFY(initiallyConfiguredStates & xdg_shell_state::fullscreen);
            QCOMPARE(initiallyConfiguredSize, QSize(1280, 1024));
            QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
        }
    }

    SECTION("place centered")
    {
        // This test verifies that Centered placement policy works.

        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("Placement", policy_to_string(win::placement::centered));
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
        QVERIFY(client);
        QCOMPARE(client->geo.frame, QRect(590, 487, 100, 50));

        shellSurface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("place under mouse")
    {
        // This test verifies that Under Mouse placement policy works.

        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("Placement", policy_to_string(win::placement::under_mouse));
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);

        cursor()->set_pos(QPoint(200, 300));
        QCOMPARE(cursor()->pos(), QPoint(200, 300));

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
        QVERIFY(client);
        QCOMPARE(client->geo.frame, QRect(151, 276, 100, 50));

        shellSurface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("place random")
    {
        // This test verifies that Random placement policy works.

        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("Placement", policy_to_string(win::placement::random));
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);

        std::unique_ptr<Surface> surface1(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
        auto client1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::red);
        QVERIFY(client1);
        QCOMPARE(client1->geo.size(), QSize(100, 50));

        std::unique_ptr<Surface> surface2(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        auto client2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(client2);
        QVERIFY(client2->geo.pos() != client1->geo.pos());
        QCOMPARE(client2->geo.size(), QSize(100, 50));

        std::unique_ptr<Surface> surface3(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface3(create_xdg_shell_toplevel(surface3));
        auto client3 = render_and_wait_for_shown(surface3, QSize(100, 50), Qt::green);
        QVERIFY(client3);
        QVERIFY(client3->geo.pos() != client1->geo.pos());
        QVERIFY(client3->geo.pos() != client2->geo.pos());
        QCOMPARE(client3->geo.size(), QSize(100, 50));

        shellSurface3.reset();
        QVERIFY(wait_for_destroyed(client3));
        shellSurface2.reset();
        QVERIFY(wait_for_destroyed(client2));
        shellSurface1.reset();
        QVERIFY(wait_for_destroyed(client1));
    }
}

}
