/*
SPDX-FileCopyrightText: 2019 David Edmundson <davidedmundson@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/placement.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

using namespace Wrapland::Client;

namespace KWin
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

struct PlaceWindowResult {
    QSize initiallyConfiguredSize;
    Wrapland::Client::xdg_shell_states initiallyConfiguredStates;
    QRect finalGeometry;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
    std::unique_ptr<Wrapland::Client::Surface> surface;
};

class TestPlacement : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void initTestCase();

    void testPlaceSmart();
    void testPlaceZeroCornered();
    void testPlaceMaximized();
    void testPlaceMaximizedLeavesFullscreen();
    void testPlaceCentered();
    void testPlaceUnderMouse();
    void testPlaceRandom();

private:
    void setPlacementPolicy(win::placement policy);
    /*
     * Create a window with the lifespan of parent and return relevant results for testing
     * defaultSize is the buffer size to use if the compositor returns an empty size in the first
     * configure event.
     */
    PlaceWindowResult createAndPlaceWindow(QSize const& defaultSize);
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

void TestPlacement::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration
                                   | Test::global_selection::plasma_shell);
    Test::cursor()->set_pos(QPoint(512, 512));
}

void TestPlacement::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestPlacement::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TestPlacement::setPlacementPolicy(win::placement policy)
{
    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("Placement", policy_to_string(policy));
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);
}

PlaceWindowResult TestPlacement::createAndPlaceWindow(QSize const& defaultSize)
{
    PlaceWindowResult rc;

    QSignalSpy window_spy(Test::app()->base->space->qobject.get(),
                          &win::space::qobject_t::wayland_window_added);
    assert(window_spy.isValid());

    // create a new window
    rc.surface = Test::create_surface();
    rc.toplevel = Test::create_xdg_shell_toplevel(rc.surface, Test::CreationSetup::CreateOnly);
    QSignalSpy configSpy(rc.toplevel.get(), &XdgShellToplevel::configured);
    assert(configSpy.isValid());

    rc.surface->commit(Surface::CommitFlag::None);
    configSpy.wait();

    auto cfgdata = rc.toplevel->get_configure_data();
    auto first_size = cfgdata.size;

    rc.toplevel->ackConfigure(configSpy.front().front().toUInt());
    configSpy.clear();

    Test::render(rc.surface, first_size.isEmpty() ? defaultSize : first_size, Qt::red);
    configSpy.wait();
    cfgdata = rc.toplevel->get_configure_data();

    auto window_id = window_spy.first().first().value<quint32>();
    auto window = Test::get_wayland_window(Test::app()->base->space->windows_map.at(window_id));

    assert(first_size.isEmpty() || first_size == cfgdata.size);
    rc.initiallyConfiguredSize = cfgdata.size;
    rc.initiallyConfiguredStates = cfgdata.states;
    rc.toplevel->ackConfigure(configSpy.front().front().toUInt());

    Test::render(rc.surface, rc.initiallyConfiguredSize, Qt::red);
    configSpy.wait(100);

    rc.finalGeometry = window->geo.frame;
    return rc;
}

void TestPlacement::testPlaceSmart()
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
        // but the goal of smart placement is to make sure windows don't overlap until they need to
        // 4 windows of 600, 500 should fit without overlap
        QVERIFY(!usedArea.intersects(placement.finalGeometry));
        usedArea += placement.finalGeometry;
    }
}

void TestPlacement::testPlaceZeroCornered()
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

void TestPlacement::testPlaceMaximized()
{
    setPlacementPolicy(win::placement::maximizing);

    // add a top panel
    std::unique_ptr<Surface> panelSurface(Test::create_surface());
    std::unique_ptr<QObject> panelShellSurface(Test::create_xdg_shell_toplevel(panelSurface));
    QVERIFY(panelSurface);
    QVERIFY(panelShellSurface);

    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaSurface->setPosition(QPoint(0, 0));
    Test::render_and_wait_for_shown(panelSurface, QSize(1280, 20), Qt::blue);

    // all windows should be initially maximized with an initial configure size sent
    std::vector<PlaceWindowResult> placements;
    for (int i = 0; i < 4; i++) {
        placements.push_back(createAndPlaceWindow(QSize(600, 500)));
        auto const& placement = placements.back();
        QVERIFY(placement.initiallyConfiguredStates & xdg_shell_state::maximized);
        QCOMPARE(placement.initiallyConfiguredSize, QSize(1280, 1024 - 20));
        QCOMPARE(placement.finalGeometry, QRect(0, 20, 1280, 1024 - 20)); // under the panel
    }
}

void TestPlacement::testPlaceMaximizedLeavesFullscreen()
{
    setPlacementPolicy(win::placement::maximizing);

    // add a top panel
    std::unique_ptr<Surface> panelSurface(Test::create_surface());
    std::unique_ptr<QObject> panelShellSurface(Test::create_xdg_shell_toplevel(panelSurface));
    QVERIFY(panelSurface);
    QVERIFY(panelShellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaSurface->setPosition(QPoint(0, 0));
    Test::render_and_wait_for_shown(panelSurface, QSize(1280, 20), Qt::blue);

    // all windows should be initially fullscreen with an initial configure size sent, despite the
    // policy
    for (int i = 0; i < 4; i++) {
        auto surface = Test::create_surface();
        auto shellSurface
            = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
        shellSurface->setFullscreen(true);

        QSignalSpy configSpy(shellSurface.get(), &XdgShellToplevel::configured);
        surface->commit(Surface::CommitFlag::None);
        configSpy.wait();

        auto cfgdata = shellSurface->get_configure_data();
        auto initiallyConfiguredSize = cfgdata.size;
        auto initiallyConfiguredStates = cfgdata.states;
        shellSurface->ackConfigure(configSpy.front().front().toUInt());

        auto c = Test::render_and_wait_for_shown(surface, initiallyConfiguredSize, Qt::red);

        QVERIFY(initiallyConfiguredStates & xdg_shell_state::fullscreen);
        QCOMPARE(initiallyConfiguredSize, QSize(1280, 1024));
        QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
    }
}

void TestPlacement::testPlaceCentered()
{
    // This test verifies that Centered placement policy works.

    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::centered));
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
    QVERIFY(client);
    QCOMPARE(client->geo.frame, QRect(590, 487, 100, 50));

    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestPlacement::testPlaceUnderMouse()
{
    // This test verifies that Under Mouse placement policy works.

    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::under_mouse));
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);

    Test::cursor()->set_pos(QPoint(200, 300));
    QCOMPARE(Test::cursor()->pos(), QPoint(200, 300));

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
    QVERIFY(client);
    QCOMPARE(client->geo.frame, QRect(151, 276, 100, 50));

    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestPlacement::testPlaceRandom()
{
    // This test verifies that Random placement policy works.

    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::random));
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);

    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::red);
    QVERIFY(client1);
    QCOMPARE(client1->geo.size(), QSize(100, 50));

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->geo.pos() != client1->geo.pos());
    QCOMPARE(client2->geo.size(), QSize(100, 50));

    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::green);
    QVERIFY(client3);
    QVERIFY(client3->geo.pos() != client1->geo.pos());
    QVERIFY(client3->geo.pos() != client2->geo.pos());
    QCOMPARE(client3->geo.size(), QSize(100, 50));

    shellSurface3.reset();
    QVERIFY(Test::wait_for_destroyed(client3));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
}

}

WAYLANDTEST_MAIN(KWin::TestPlacement)
#include "placement_test.moc"
