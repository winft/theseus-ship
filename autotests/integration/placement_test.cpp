/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2019 David Edmundson <davidedmundson@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "screens.h"
#include "win/placement.h"
#include "win/space.h"
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

struct PlaceWindowResult {
    QSize initiallyConfiguredSize;
    Wrapland::Client::XdgShellToplevel::States initiallyConfiguredStates;
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
    input::get_cursor()->set_pos(QPoint(512, 512));
}

void TestPlacement::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestPlacement::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TestPlacement::setPlacementPolicy(win::placement policy)
{
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("Placement", policy_to_string(policy));
    group.sync();
    workspace()->slotReconfigure();
}

PlaceWindowResult TestPlacement::createAndPlaceWindow(QSize const& defaultSize)
{
    PlaceWindowResult rc;

    QSignalSpy window_spy(static_cast<win::wayland::space*>(workspace()),
                          &win::wayland::space::wayland_window_added);
    assert(window_spy.isValid());

    // create a new window
    rc.surface = Test::create_surface();
    rc.toplevel = Test::create_xdg_shell_toplevel(rc.surface, Test::CreationSetup::CreateOnly);
    QSignalSpy configSpy(rc.toplevel.get(), &XdgShellToplevel::configureRequested);
    assert(configSpy.isValid());

    rc.surface->commit(Surface::CommitFlag::None);
    configSpy.wait();

    // First configure is always sent with empty size.
    assert(configSpy[0][0].toSize().isEmpty());
    rc.toplevel->ackConfigure(configSpy[0][2].toUInt());
    configSpy.clear();

    Test::render(rc.surface, defaultSize, Qt::red);
    configSpy.wait();

    auto window = window_spy.first().first().value<win::wayland::window*>();

    rc.initiallyConfiguredSize = configSpy[0][0].toSize();
    rc.initiallyConfiguredStates
        = configSpy[0][1].value<Wrapland::Client::XdgShellToplevel::States>();
    rc.toplevel->ackConfigure(configSpy[0][2].toUInt());

    Test::render(rc.surface, rc.initiallyConfiguredSize, Qt::red);
    configSpy.wait(100);

    rc.finalGeometry = window->frameGeometry();
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
        QVERIFY(placement.initiallyConfiguredStates & XdgShellToplevel::State::Maximized);
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
        QSignalSpy configSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
        surface->commit(Surface::CommitFlag::None);
        configSpy.wait();

        auto initiallyConfiguredSize = configSpy[0][0].toSize();
        auto initiallyConfiguredStates
            = configSpy[0][1].value<Wrapland::Client::XdgShellToplevel::States>();
        shellSurface->ackConfigure(configSpy[0][2].toUInt());

        auto c = Test::render_and_wait_for_shown(surface, initiallyConfiguredSize, Qt::red);

        QVERIFY(initiallyConfiguredStates & XdgShellToplevel::State::Fullscreen);
        QCOMPARE(initiallyConfiguredSize, QSize(1280, 1024));
        QCOMPARE(c->frameGeometry(), QRect(0, 0, 1280, 1024));
    }
}

void TestPlacement::testPlaceCentered()
{
    // This test verifies that Centered placement policy works.

    KConfigGroup group = kwinApp()->config()->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::centered));
    group.sync();
    workspace()->slotReconfigure();

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
    QVERIFY(client);
    QCOMPARE(client->frameGeometry(), QRect(590, 487, 100, 50));

    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestPlacement::testPlaceUnderMouse()
{
    // This test verifies that Under Mouse placement policy works.

    KConfigGroup group = kwinApp()->config()->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::under_mouse));
    group.sync();
    workspace()->slotReconfigure();

    input::get_cursor()->set_pos(QPoint(200, 300));
    QCOMPARE(input::get_cursor()->pos(), QPoint(200, 300));

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
    QVERIFY(client);
    QCOMPARE(client->frameGeometry(), QRect(151, 276, 100, 50));

    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestPlacement::testPlaceRandom()
{
    // This test verifies that Random placement policy works.

    KConfigGroup group = kwinApp()->config()->group("Windows");
    group.writeEntry("Placement", policy_to_string(win::placement::random));
    group.sync();
    workspace()->slotReconfigure();

    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::red);
    QVERIFY(client1);
    QCOMPARE(client1->size(), QSize(100, 50));

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    QVERIFY(client2);
    QVERIFY(client2->pos() != client1->pos());
    QCOMPARE(client2->size(), QSize(100, 50));

    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::green);
    QVERIFY(client3);
    QVERIFY(client3->pos() != client1->pos());
    QVERIFY(client3->pos() != client2->pos());
    QCOMPARE(client3->size(), QSize(100, 50));

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
