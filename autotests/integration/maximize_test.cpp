/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "win/active_window.h"
#include "win/deco.h"
#include "win/deco/bridge.h"
#include "win/deco/settings.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <Wrapland/Server/xdg_decoration.h>

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

using namespace Wrapland::Client;

namespace KWin
{

class TestMaximized : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMaximizedPassedToDeco();
    void testInitiallyMaximized();
    void testInitiallyMaximizedBorderless();
    void testBorderlessMaximizedWindow();
    void testBorderlessMaximizedWindowNoClientSideDecoration();
};

void TestMaximized::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TestMaximized::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration
                                   | Test::global_selection::plasma_shell);

    Test::cursor()->set_pos(QPoint(1280, 512));
}

void TestMaximized::cleanup()
{
    Test::destroy_wayland_connection();

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", false);
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(kwinApp()->options->qobject->borderlessMaximizedWindows(), false);
}

void TestMaximized::testMaximizedPassedToDeco()
{
    // this test verifies that when a xdg-shell toplevel gets maximized the Decoration receives the
    // signal

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(),
                                                                        shellSurface.get());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(win::decoration(client));

    auto decoration = win::decoration(client);
    QVERIFY(decoration);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

    // Wait for configure event that signals the client is active now.
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    // When there are no borders, there is no change to them when maximizing.
    // TODO: we should test both cases with fixed fake decoration for autotests.
    auto const hasBorders
        = Test::app()->base.space->deco->settings()->borderSize() != KDecoration2::BorderSize::None;

    // now maximize
    QSignalSpy bordersChangedSpy(decoration, &KDecoration2::Decoration::bordersChanged);
    QVERIFY(bordersChangedSpy.isValid());
    QSignalSpy maximizedChangedSpy(decoration->client().data(),
                                   &KDecoration2::DecoratedClient::maximizedChanged);
    QVERIFY(maximizedChangedSpy.isValid());
    QSignalSpy geometryShapeChangedSpy(client->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryShapeChangedSpy.isValid());

    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    auto cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(1280, 1024 - decoration->borderTop()));

    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
    Test::render(surface, cfgdata.size, Qt::red);

    QVERIFY(geometryShapeChangedSpy.wait());

    // If no borders, there is only the initial geometry shape change, but none through border
    // resizing.
    QCOMPARE(geometryShapeChangedSpy.count(), hasBorders ? 3 : 1);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(maximizedChangedSpy.count(), 1);
    QCOMPARE(maximizedChangedSpy.last().first().toBool(), true);
    QCOMPARE(bordersChangedSpy.count(), hasBorders ? 1 : 0);
    QCOMPARE(decoration->borderLeft(), 0);
    QCOMPARE(decoration->borderBottom(), 0);
    QCOMPARE(decoration->borderRight(), 0);
    QVERIFY(decoration->borderTop() != 0);

    // now unmaximize again
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(100, 50));

    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::red);
    QVERIFY(geometryShapeChangedSpy.wait());
    QCOMPARE(geometryShapeChangedSpy.count(), hasBorders ? 6 : 2);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(maximizedChangedSpy.count(), 2);
    QCOMPARE(maximizedChangedSpy.last().first().toBool(), false);
    QCOMPARE(bordersChangedSpy.count(), hasBorders ? 2 : 0);
    QVERIFY(decoration->borderTop() != 0);
    QVERIFY(decoration->borderLeft() != !hasBorders);
    QVERIFY(decoration->borderRight() != !hasBorders);
    QVERIFY(decoration->borderBottom() != !hasBorders);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestMaximized::testInitiallyMaximized()
{
    // This test verifies that a window created as maximized, will be maximized.

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));

    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());

    shellSurface->setMaximized(true);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    auto cfgdata = shellSurface->get_configure_data();
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

    // Now let's render in an incorrect size.
    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->geo.frame, QRect(0, 0, 100, 50));
    QEXPECT_FAIL("", "Should go out of maximzied", Continue);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestMaximized::testInitiallyMaximizedBorderless()
{
    // This test verifies that a window created as maximized, will be maximized and without Border
    // with BorderlessMaximizedWindows

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(kwinApp()->options->qobject->borderlessMaximizedWindows(), true);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));
    std::unique_ptr<XdgDecoration> decoration(
        Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get()));

    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());

    shellSurface->setMaximized(true);

    QSignalSpy decorationConfiguredSpy(decoration.get(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());

    decoration->setMode(XdgDecoration::Mode::ServerSide);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    auto cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(1280, 1024));
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(!win::decoration(client));
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->geo.frame, QRect(0, 0, 1280, 1024));

    QTRY_VERIFY(decorationConfiguredSpy.count());
    QCOMPARE(decoration->mode(), XdgDecoration::Mode::ServerSide);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}
void TestMaximized::testBorderlessMaximizedWindow()
{
    // This test verifies that a maximized client looses it's server-side
    // decoration when the borderless maximized option is on.

    // Enable the borderless maximized windows option.
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(kwinApp()->options->qobject->borderlessMaximizedWindows(), true);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));
    std::unique_ptr<XdgDecoration> decoration(
        Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get()));

    QSignalSpy decorationConfiguredSpy(decoration.get(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());

    decoration->setMode(XdgDecoration::Mode::ServerSide);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    auto cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(0, 0));
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QVERIFY(win::decoration(client));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    cfgdata = shellSurface->get_configure_data();
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

    // Maximize the client.
    const QRect maximizeRestoreGeometry = client->geo.frame;
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(1280, 1024));
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());

    Test::render(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->geo.frame, QRect(0, 0, 1280, 1024));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QVERIFY(!win::decoration(client));

    // Restore the client.
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);

    cfgdata = shellSurface->get_configure_data();
    QCOMPARE(cfgdata.size, QSize(100, 50));
    QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
    QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

    shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::red);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->geo.frame, maximizeRestoreGeometry);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QVERIFY(win::decoration(client));

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestMaximized::testBorderlessMaximizedWindowNoClientSideDecoration()
{
    // test case verifies that borderless maximized windows doesn't cause
    // clients to render client-side decorations instead (BUG 405385)

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(kwinApp()->options->qobject->borderlessMaximizedWindows(), true);

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> xdgShellToplevel(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<XdgDecoration> deco(
        Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
            xdgShellToplevel.get()));

    QSignalSpy decorationConfiguredSpy(deco.get(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy configureRequestedSpy(xdgShellToplevel.get(), &XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());

    QVERIFY(win::decoration(client));
    QVERIFY(!client->noBorder());
    configureRequestedSpy.wait();
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(decorationConfiguredSpy.count(), 1);

    auto cfgdata = xdgShellToplevel->get_configure_data();
    QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    // go to maximized
    xdgShellToplevel->setMaximized(true);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    cfgdata = xdgShellToplevel->get_configure_data();
    QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));

    for (auto const& sig : configureRequestedSpy) {
        xdgShellToplevel->ackConfigure(sig.front().toInt());
    }

    Test::render(surface, cfgdata.size, Qt::red);
    QVERIFY(geometryChangedSpy.wait());

    // no deco
    QVERIFY(!win::decoration(client));
    QVERIFY(client->noBorder());
    // but still server-side
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    // go back to normal
    xdgShellToplevel->setMaximized(false);

    configureRequestedSpy.wait();
    QCOMPARE(configureRequestedSpy.count(), 3);
    cfgdata = xdgShellToplevel->get_configure_data();

    for (auto const& sig : configureRequestedSpy) {
        xdgShellToplevel->ackConfigure(sig.front().toInt());
    }
    Test::render(surface, cfgdata.size, Qt::red);
    QVERIFY(geometryChangedSpy.wait());

    QVERIFY(win::decoration(client));
    QVERIFY(!client->noBorder());
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
}

}

WAYLANDTEST_MAIN(KWin::TestMaximized)
#include "maximize_test.moc"
