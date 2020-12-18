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
#include "kwin_wayland_test.h"
#include "cursor.h"
#include "decorations/decorationbridge.h"
#include "decorations/settings.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/deco.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/plasmashell.h>

#include <Wrapland/Server/xdg_decoration.h>

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

using namespace KWin;
using namespace Wrapland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_maximized-0");

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
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    waylandServer()->initWorkspace();
}

void TestMaximized::init()
{
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::XdgDecoration |
                                 Test::AdditionalWaylandInterface::PlasmaShell);

    screens()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(1280, 512));
}

void TestMaximized::cleanup()
{
    Test::destroyWaylandConnection();

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", false);
    group.sync();
    Workspace::self()->slotReconfigure();
    QCOMPARE(options->borderlessMaximizedWindows(), false);
}

void TestMaximized::testMaximizedPassedToDeco()
{
    // this test verifies that when a xdg-shell toplevel gets maximized the Decoration receives the signal

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellSurface> shellSurface(Test::createXdgShellStableSurface(surface.data()));
    Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());

    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(win::decoration(client));

    auto decoration = win::decoration(client);
    QVERIFY(decoration);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

    // Wait for configure event that signals the client is active now.
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    // When there are no borders, there is no change to them when maximizing.
    // TODO: we should test both cases with fixed fake decoration for autotests.
    const bool hasBorders = Decoration::DecorationBridge::self()->settings()->borderSize() != KDecoration2::BorderSize::None;

    // now maximize
    QSignalSpy bordersChangedSpy(decoration, &KDecoration2::Decoration::bordersChanged);
    QVERIFY(bordersChangedSpy.isValid());
    QSignalSpy maximizedChangedSpy(decoration->client().data(), &KDecoration2::DecoratedClient::maximizedChanged);
    QVERIFY(maximizedChangedSpy.isValid());
    QSignalSpy geometryShapeChangedSpy(client, &Toplevel::geometryShapeChanged);
    QVERIFY(geometryShapeChangedSpy.isValid());

    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024 - decoration->borderTop()));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), configureRequestedSpy.last().at(0).toSize(), Qt::red);
    QVERIFY(geometryShapeChangedSpy.wait());

    // If no borders, there is only the initial geometry shape change, but none through border resizing.
    // TODO: for some reason there is one more shape changed signal (size going from 0,0 to -1,-1)
    QCOMPARE(geometryShapeChangedSpy.count(), hasBorders ? 3 : 2);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(maximizedChangedSpy.count(), 1);
    QCOMPARE(maximizedChangedSpy.last().first().toBool(), true);
    QCOMPARE(bordersChangedSpy.count(), hasBorders ? 1 : 0);
    QCOMPARE(decoration->borderLeft(), 0);
    QCOMPARE(decoration->borderBottom(), 0);
    QCOMPARE(decoration->borderRight(), 0);
    QVERIFY(decoration->borderTop() != 0);

    // now unmaximize again
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(100, 50));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(100, 50), Qt::red);
    QVERIFY(geometryShapeChangedSpy.wait());
    QCOMPARE(geometryShapeChangedSpy.count(), hasBorders ? 6 : 4);
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
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestMaximized::testInitiallyMaximized()
{
    // This test verifies that a window created as maximized, will be maximized.

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellSurface> shellSurface(
        Test::createXdgShellStableSurface(surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    shellSurface->setMaximized(true);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellSurface::States states;
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(!states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(states.testFlag(XdgShellSurface::State::Maximized));

    // Now let's render in an incorrect size.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->frameGeometry(), QRect(0, 0, 100, 50));
    QEXPECT_FAIL("", "Should go out of maximzied", Continue);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestMaximized::testInitiallyMaximizedBorderless()
{
    // This test verifies that a window created as maximized, will be maximized and without Border with BorderlessMaximizedWindows

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    Workspace::self()->slotReconfigure();
    QCOMPARE(options->borderlessMaximizedWindows(), true);

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellSurface> shellSurface(
        Test::createXdgShellStableSurface(surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QScopedPointer<XdgDecoration> decoration(
        Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data()));

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    shellSurface->setMaximized(true);
    QSignalSpy decorationConfiguredSpy(decoration.data(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());
    decoration->setMode(XdgDecoration::Mode::ServerSide);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellSurface::States states;
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(!states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(states.testFlag(XdgShellSurface::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(!win::decoration(client));
    QVERIFY(client->control()->active());
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->requestedMaximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->frameGeometry(), QRect(0, 0, 1280, 1024));

    QTRY_VERIFY(decorationConfiguredSpy.count());
    QCOMPARE(decoration->mode(), XdgDecoration::Mode::ServerSide);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}
void TestMaximized::testBorderlessMaximizedWindow()
{
    // This test verifies that a maximized client looses it's server-side
    // decoration when the borderless maximized option is on.

    // Enable the borderless maximized windows option.
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    Workspace::self()->slotReconfigure();
    QCOMPARE(options->borderlessMaximizedWindows(), true);

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellSurface> shellSurface(
        Test::createXdgShellStableSurface(surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QScopedPointer<XdgDecoration> decoration(
        Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data()));
    QSignalSpy decorationConfiguredSpy(decoration.data(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    decoration->setMode(XdgDecoration::Mode::ServerSide);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellSurface::States states;
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(0, 0));
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(!states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(!states.testFlag(XdgShellSurface::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control()->active());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->requestedMaximizeMode(), win::maximize_mode::restore);
    QVERIFY(win::decoration(client));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(!states.testFlag(XdgShellSurface::State::Maximized));

    // Maximize the client.
    const QRect maximizeRestoreGeometry = client->frameGeometry();
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(states.testFlag(XdgShellSurface::State::Maximized));

    QSignalSpy geometryChangedSpy(client, &Toplevel::geometryChanged);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(1280, 1024), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->frameGeometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->requestedMaximizeMode(), win::maximize_mode::full);
    QVERIFY(!win::decoration(client));

    // Restore the client.
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(100, 50));
    states = configureRequestedSpy.last().at(1).value<XdgShellSurface::States>();
    QVERIFY(states.testFlag(XdgShellSurface::State::Activated));
    QVERIFY(!states.testFlag(XdgShellSurface::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(100, 50), Qt::red);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->frameGeometry(), maximizeRestoreGeometry);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->requestedMaximizeMode(), win::maximize_mode::restore);
    QVERIFY(win::decoration(client));

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestMaximized::testBorderlessMaximizedWindowNoClientSideDecoration()
{
    // test case verifies that borderless maximized windows doesn't cause
    // clients to render client-side decorations instead (BUG 405385)

    // adjust config
    auto group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    Workspace::self()->slotReconfigure();
    QCOMPARE(options->borderlessMaximizedWindows(), true);

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellSurface> xdgShellSurface(Test::createXdgShellStableSurface(surface.data()));
    QScopedPointer<XdgDecoration> deco(Test::xdgDecorationManager()->getToplevelDecoration(xdgShellSurface.data()));

    QSignalSpy decorationConfiguredSpy(deco.data(), &XdgDecoration::modeChanged);
    QVERIFY(decorationConfiguredSpy.isValid());

    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);

    QSignalSpy geometryChangedSpy(client, &win::wayland::window::geometryChanged);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy sizeChangeRequestedSpy(xdgShellSurface.data(), &XdgShellSurface::sizeChanged);
    QVERIFY(sizeChangeRequestedSpy.isValid());
    QSignalSpy configureRequestedSpy(xdgShellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());

    QVERIFY(win::decoration(client));
    QVERIFY(!client->noBorder());
    configureRequestedSpy.wait();
    QCOMPARE(decorationConfiguredSpy.count(), 1);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    // go to maximized
    xdgShellSurface->setMaximized(true);
    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 1);

    for (const auto &it: configureRequestedSpy) {
        xdgShellSurface->ackConfigure(it[2].toInt());
    }
    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);
    QVERIFY(geometryChangedSpy.wait());

    // no deco
    QVERIFY(!win::decoration(client));
    QVERIFY(client->noBorder());
    // but still server-side
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    // go back to normal
    xdgShellSurface->setMaximized(false);
    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 2);

    for (const auto &it: configureRequestedSpy) {
        xdgShellSurface->ackConfigure(it[2].toInt());
    }
    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);
    QVERIFY(geometryChangedSpy.wait());

    QVERIFY(win::decoration(client));
    QVERIFY(!client->noBorder());
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
}

WAYLANDTEST_MAIN(TestMaximized)
#include "maximize_test.moc"
