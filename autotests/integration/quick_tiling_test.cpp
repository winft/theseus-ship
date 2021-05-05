/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "platform.h"
#include "cursor.h"
#include "decorations/decorationbridge.h"
#include "decorations/settings.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include "scripting/scripting.h"

#include "win/move.h"
#include "win/screen.h"
#include "win/x11/window.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgshell.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QTemporaryFile>
#include <QTextStream>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

#include <linux/input.h>

Q_DECLARE_METATYPE(KWin::win::quicktiles)
Q_DECLARE_METATYPE(KWin::win::maximize_mode)

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_quick_tiling-0");

class QuickTilingTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testQuickTiling_data();
    void testQuickTiling();
    void testQuickMaximizing_data();
    void testQuickMaximizing();
    void testQuickTilingKeyboardMove_data();
    void testQuickTilingKeyboardMove();
    void testQuickTilingPointerMove_data();
    void testQuickTilingPointerMove();
    void testQuickTilingTouchMove_data();
    void testQuickTilingTouchMove();
    void testX11QuickTiling_data();
    void testX11QuickTiling();
    void testX11QuickTilingAfterVertMaximize_data();
    void testX11QuickTilingAfterVertMaximize();
    void testShortcut_data();
    void testShortcut();
    void testScript_data();
    void testScript();

private:
    Wrapland::Client::ConnectionThread *m_connection = nullptr;
    Wrapland::Client::Compositor *m_compositor = nullptr;
};

void QuickTilingTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();
    qRegisterMetaType<KWin::Toplevel*>();
    qRegisterMetaType<KWin::win::maximize_mode>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    // set custom config which disables the Outline
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config->group("Outline");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    kwinApp()->setConfig(config);

    qputenv("XKB_DEFAULT_RULES", "evdev");

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
}

void QuickTilingTest::init()
{
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::XdgDecoration);
    m_connection = Test::waylandConnection();
    m_compositor = Test::waylandCompositor();

    screens()->setCurrent(0);
}

void QuickTilingTest::cleanup()
{
    Test::destroyWaylandConnection();
}

struct XcbConnectionDeleter
{
    static inline void cleanup(xcb_connection_t *pointer)
    {
        xcb_disconnect(pointer);
    }
};

void QuickTilingTest::testQuickTiling_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");
    QTest::addColumn<QRect>("secondScreen");
    QTest::addColumn<win::quicktiles>("expectedModeAfterToggle");

    QTest::newRow("left")   << win::quicktiles::left   << QRect(0, 0, 640, 1024)   << QRect(1280, 0, 640, 1024) << win::quicktiles::right;
    QTest::newRow("top")    << win::quicktiles::top    << QRect(0, 0, 1280, 512)   << QRect(1280, 0, 1280, 512) << win::quicktiles::top;
    QTest::newRow("right")  << win::quicktiles::right  << QRect(640, 0, 640, 1024) << QRect(1920, 0, 640, 1024) << win::quicktiles::none;
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512) << QRect(1280, 512, 1280, 512) << win::quicktiles::bottom;

    QTest::newRow("top left")     << (win::quicktiles::left  | win::quicktiles::top)    << QRect(0, 0, 640, 512)     << QRect(1280, 0, 640, 512) << (win::quicktiles::right | win::quicktiles::top);
    QTest::newRow("top right")    << (win::quicktiles::right | win::quicktiles::top)    << QRect(640, 0, 640, 512)   << QRect(1920, 0, 640, 512) << win::quicktiles::none;
    QTest::newRow("bottom left")  << (win::quicktiles::left  | win::quicktiles::bottom) << QRect(0, 512, 640, 512)   << QRect(1280, 512, 640, 512) << (win::quicktiles::right  | win::quicktiles::bottom);
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom) << QRect(640, 512, 640, 512) << QRect(1920, 512, 640, 512) << win::quicktiles::none;

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << win::quicktiles::none;
}

void QuickTilingTest::testQuickTiling()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Map the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    // We have to receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QFETCH(win::quicktiles, mode);
    QFETCH(QRect, expectedGeometry);

    win::set_quicktile_mode(c, mode, true);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // at this point the geometry did not yet change
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));

    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling(), mode);

    // but we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());

    // attach a new image
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->frameGeometry(), expectedGeometry);

    // send window to other screen
    QCOMPARE(c->screen(), 0);
    win::send_to_screen(c, 1);
    QCOMPARE(c->screen(), 1);

    // quick tile should not be changed
    QCOMPARE(c->control->quicktiling(), mode);
    QTEST(c->frameGeometry(), "secondScreen");

    // now try to toggle again
    win::set_quicktile_mode(c, mode, true);
    QTEST(c->control->quicktiling(), "expectedModeAfterToggle");
}

void QuickTilingTest::testQuickMaximizing_data()
{
    QTest::addColumn<win::quicktiles>("mode");

    QTest::newRow("maximize") << win::quicktiles::maximize;
    QTest::newRow("none") << win::quicktiles::none;
}

void QuickTilingTest::testQuickMaximizing()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Map the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // We have to receive a configure event upon becoming active.
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy maximizeChangedSpy1(c,
        qOverload<Toplevel*, win::maximize_mode>(&Toplevel::clientMaximizedStateChanged));
    QVERIFY(maximizeChangedSpy1.isValid());
    QSignalSpy maximizeChangedSpy2(c,
        qOverload<Toplevel*, bool, bool>(&Toplevel::clientMaximizedStateChanged));
    QVERIFY(maximizeChangedSpy2.isValid());

    // Now quicktile-maximize.
    win::set_quicktile_mode(c, win::quicktiles::maximize, true);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // At this point the geometry did not yet change.
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling(), win::quicktiles::maximize);
    QCOMPARE(c->restore_geometries.maximize, QRect(0, 0, 100, 50));

    // But we got requested a new geometry.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));

    // Attach a new image.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), configureRequestedSpy.last().at(0).toSize(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(c->restore_geometries.maximize, QRect(0, 0, 100, 50));

    // client is now set to maximised
    QCOMPARE(maximizeChangedSpy1.count(), 1);
    QCOMPARE(maximizeChangedSpy1.first().first().value<KWin::Toplevel*>(), c);
    QCOMPARE(maximizeChangedSpy1.first().last().value<KWin::win::maximize_mode>(), win::maximize_mode::full);
    QCOMPARE(maximizeChangedSpy2.count(), 1);
    QCOMPARE(maximizeChangedSpy2.first().first().value<KWin::Toplevel*>(), c);
    QCOMPARE(maximizeChangedSpy2.first().at(1).toBool(), true);
    QCOMPARE(maximizeChangedSpy2.first().at(2).toBool(), true);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::full);

    // go back to quick tile none
    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(c, mode, true);
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);
    QCOMPARE(quickTileChangedSpy.count(), 2);

    // geometry not yet changed
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(c->restore_geometries.maximize, QRect());

    // we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(100, 50));

    // render again
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(100, 50), Qt::yellow);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 2);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(maximizeChangedSpy1.count(), 2);
    QCOMPARE(maximizeChangedSpy1.last().first().value<KWin::Toplevel*>(), c);
    QCOMPARE(maximizeChangedSpy1.last().last().value<KWin::win::maximize_mode>(), win::maximize_mode::restore);
    QCOMPARE(maximizeChangedSpy2.count(), 2);
    QCOMPARE(maximizeChangedSpy2.last().first().value<KWin::Toplevel*>(), c);
    QCOMPARE(maximizeChangedSpy2.last().at(1).toBool(), false);
    QCOMPARE(maximizeChangedSpy2.last().at(2).toBool(), false);
}

void QuickTilingTest::testQuickTilingKeyboardMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24) << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023) << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023) << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingKeyboardMove()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());

    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());
    QSignalSpy sizeChangeSpy(shellSurface.data(), &XdgShellSurface::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());
    // let's render
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    workspace()->performWindowOperation(c, Options::UnrestrictedMoveOp);
    QCOMPARE(c, workspace()->moveResizeClient());
    QCOMPARE(Cursor::pos(), QPoint(49, 24));

    QFETCH(QPoint, targetPos);
    quint32 timestamp = 1;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTCTRL, timestamp++);
    while (Cursor::pos().x() > targetPos.x()) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_LEFT, timestamp++);
        kwinApp()->platform()->keyboardKeyReleased(KEY_LEFT, timestamp++);
    }
    while (Cursor::pos().x() < targetPos.x()) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
        kwinApp()->platform()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    }
    while (Cursor::pos().y() < targetPos.y()) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_DOWN, timestamp++);
        kwinApp()->platform()->keyboardKeyReleased(KEY_DOWN, timestamp++);
    }
    while (Cursor::pos().y() > targetPos.y()) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_UP, timestamp++);
        kwinApp()->platform()->keyboardKeyReleased(KEY_UP, timestamp++);
    }
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTCTRL, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_ENTER, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_ENTER, timestamp++);
    QCOMPARE(Cursor::pos(), targetPos);
    QVERIFY(!workspace()->moveResizeClient());

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling(), "expectedMode");
}

void QuickTilingTest::testQuickTilingPointerMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24) << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023) << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023) << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingPointerMove()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());

    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(
        surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QVERIFY(!shellSurface.isNull());

    // wait for the initial configure event
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    // let's render
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // we have to receive a configure event when the client becomes active
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), 2);

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    workspace()->performWindowOperation(c, Options::UnrestrictedMoveOp);
    QCOMPARE(c, workspace()->moveResizeClient());
    QCOMPARE(Cursor::pos(), QPoint(49, 24));
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    QFETCH(QPoint, targetPos);
    quint32 timestamp = 1;
    kwinApp()->platform()->pointerMotion(targetPos, timestamp++);
    kwinApp()->platform()->pointerButtonPressed(BTN_LEFT, timestamp++);
    kwinApp()->platform()->pointerButtonReleased(BTN_LEFT, timestamp++);
    QCOMPARE(Cursor::pos(), targetPos);
    QVERIFY(!workspace()->moveResizeClient());

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling(), "expectedMode");
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    QCOMPARE(false, configureRequestedSpy.last().first().toSize().isEmpty());
}

void QuickTilingTest::testQuickTilingTouchMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24) << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023) << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023) << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingTouchMove()
{
    // test verifies that touch on decoration also allows quick tiling
    // see BUG: 390113
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());

    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(
        surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QVERIFY(!shellSurface.isNull());

    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());

    deco->setMode(XdgDecoration::Mode::ServerSide);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());

    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
    QCOMPARE(configureRequestedSpy.count(), 1);
    QVERIFY(configureRequestedSpy.last().first().toSize().isEmpty());

    // let's render
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(1000, 50), Qt::blue);

    QVERIFY(c);
    QVERIFY(win::decoration(c));
    auto const decoration = win::decoration(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(-decoration->borderLeft(), 0,
                                       1000 + decoration->borderLeft() + decoration->borderRight(),
                                       50 + decoration->borderTop() + decoration->borderBottom()));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // we have to receive a configure event when the client becomes active
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), 2);

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    quint32 timestamp = 1;
    kwinApp()->platform()->touchDown(0, QPointF(c->frameGeometry().center().x(), c->frameGeometry().y() + decoration->borderTop() / 2), timestamp++);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(c, workspace()->moveResizeClient());
    QCOMPARE(configureRequestedSpy.count(), 3);

    QFETCH(QPoint, targetPos);
    kwinApp()->platform()->touchMotion(0, targetPos, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(!workspace()->moveResizeClient());


    // When there are no borders, there is no change to them when quick-tiling.
    // TODO: we should test both cases with fixed fake decoration for autotests.
    const bool hasBorders = Decoration::DecorationBridge::self()->settings()->borderSize() != KDecoration2::BorderSize::None;

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling(), "expectedMode");
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), hasBorders ? 5 : 4);
    QCOMPARE(false, configureRequestedSpy.last().first().toSize().isEmpty());
}

void QuickTilingTest::testX11QuickTiling_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");
    QTest::addColumn<int>("screen");
    QTest::addColumn<win::quicktiles>("modeAfterToggle");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)

    QTest::newRow("left")   << win::quicktiles::left   << QRect(0, 0, 640, 1024) << 0 << win::quicktiles::none;
    QTest::newRow("top")    << win::quicktiles::top    << QRect(0, 0, 1280, 512) << 1 << win::quicktiles::top;
    QTest::newRow("right")  << win::quicktiles::right  << QRect(640, 0, 640, 1024) << 1 << win::quicktiles::left;
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512) << 1 << win::quicktiles::bottom;

    QTest::newRow("top left")     << (win::quicktiles::left  | win::quicktiles::top)    << QRect(0, 0, 640, 512) << 0 << win::quicktiles::none;
    QTest::newRow("top right")    << (win::quicktiles::right | win::quicktiles::top)    << QRect(640, 0, 640, 512) << 1 << (win::quicktiles::left | win::quicktiles::top);
    QTest::newRow("bottom left")  << (win::quicktiles::left  | win::quicktiles::bottom) << QRect(0, 512, 640, 512) << 0 << win::quicktiles::none;
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom) << QRect(640, 512, 640, 512) << 1 << (win::quicktiles::left | win::quicktiles::bottom);

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024) << 0 << win::quicktiles::none;

#undef FLAG
}
void QuickTilingTest::testX11QuickTiling()
{
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.data());
    xcb_create_window(c.data(), XCB_COPY_FROM_PARENT, w, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);

    auto const origGeo = client->frameGeometry();

    // now quick tile
    QSignalSpy quickTileChangedSpy(client, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(client, mode, true);

    QCOMPARE(client->control->quicktiling(), mode);
    QTEST(client->frameGeometry(), "expectedGeometry");
    QCOMPARE(client->restore_geometries.maximize, origGeo);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    QCOMPARE(client->screen(), 0);
    QFETCH(win::quicktiles, modeAfterToggle);

    // quick tile to same edge again should also act like send to screen
    win::set_quicktile_mode(client, mode, true);
    QTEST(client->screen(), "screen");
    QCOMPARE(client->control->quicktiling(), modeAfterToggle);
    QCOMPARE(client->restore_geometries.maximize.isValid(), modeAfterToggle != win::quicktiles::none);
    QCOMPARE(client->restore_geometries.maximize, modeAfterToggle != win::quicktiles::none ? origGeo : QRect());

    // and destroy the window again
    xcb_unmap_window(c.data(), w);
    xcb_destroy_window(c.data(), w);
    xcb_flush(c.data());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void QuickTilingTest::testX11QuickTilingAfterVertMaximize_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");

    QTest::newRow("left")   << win::quicktiles::left   << QRect(0, 0, 640, 1024);
    QTest::newRow("top")    << win::quicktiles::top    << QRect(0, 0, 1280, 512);
    QTest::newRow("right")  << win::quicktiles::right  << QRect(640, 0, 640, 1024);
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512);

    QTest::newRow("top left")     << (win::quicktiles::left  | win::quicktiles::top)    << QRect(0, 0, 640, 512);
    QTest::newRow("top right")    << (win::quicktiles::right | win::quicktiles::top)    << QRect(640, 0, 640, 512);
    QTest::newRow("bottom left")  << (win::quicktiles::left  | win::quicktiles::bottom) << QRect(0, 512, 640, 512);
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom) << QRect(640, 512, 640, 512);

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024);
}

void QuickTilingTest::testX11QuickTilingAfterVertMaximize()
{
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.data());
    xcb_create_window(c.data(), XCB_COPY_FROM_PARENT, w, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);

    const QRect origGeo = client->frameGeometry();
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    // vertically maximize the window
    win::maximize(client, win::flags(client->maximizeMode() ^ win::maximize_mode::vertical));
    QCOMPARE(client->frameGeometry().width(), origGeo.width());
    QCOMPARE(client->size().height(), screens()->size(client->screen()).height());
    QCOMPARE(client->restore_geometries.maximize, origGeo);

    // now quick tile
    QSignalSpy quickTileChangedSpy(client, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(client, mode, true);
    QCOMPARE(client->control->quicktiling(), mode);
    QTEST(client->frameGeometry(), "expectedGeometry");
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // and destroy the window again
    xcb_unmap_window(c.data(), w);
    xcb_destroy_window(c.data(), w);
    xcb_flush(c.data());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void QuickTilingTest::testShortcut_data()
{
    QTest::addColumn<QStringList>("shortcutList");
    QTest::addColumn<win::quicktiles>("expectedMode");
    QTest::addColumn<QRect>("expectedGeometry");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)
    QTest::newRow("top") << QStringList{QStringLiteral("Window Quick Tile Top")} << win::quicktiles::top << QRect(0, 0, 1280, 512);
    QTest::newRow("bottom") << QStringList{QStringLiteral("Window Quick Tile Bottom")} << win::quicktiles::bottom << QRect(0, 512, 1280, 512);
    QTest::newRow("top right") << QStringList{QStringLiteral("Window Quick Tile Top Right")} << (win::quicktiles::top | win::quicktiles::right) << QRect(640, 0, 640, 512);
    QTest::newRow("top left") << QStringList{QStringLiteral("Window Quick Tile Top Left")} << (win::quicktiles::top | win::quicktiles::left) << QRect(0, 0, 640, 512);
    QTest::newRow("bottom right") << QStringList{QStringLiteral("Window Quick Tile Bottom Right")} << (win::quicktiles::bottom | win::quicktiles::right) << QRect(640, 512, 640, 512);
    QTest::newRow("bottom left") << QStringList{QStringLiteral("Window Quick Tile Bottom Left")} << (win::quicktiles::bottom | win::quicktiles::left) << QRect(0, 512, 640, 512);
    QTest::newRow("left") << QStringList{QStringLiteral("Window Quick Tile Left")} << win::quicktiles::left << QRect(0, 0, 640, 1024);
    QTest::newRow("right") << QStringList{QStringLiteral("Window Quick Tile Right")} << win::quicktiles::right << QRect(640, 0, 640, 1024);

    // Test combined actions for corner tiling
    QTest::newRow("top left combined") << QStringList{QStringLiteral("Window Quick Tile Left"), QStringLiteral("Window Quick Tile Top")} << (win::quicktiles::top | win::quicktiles::left) << QRect(0, 0, 640, 512);
    QTest::newRow("top right combined") << QStringList{QStringLiteral("Window Quick Tile Right"), QStringLiteral("Window Quick Tile Top")} << (win::quicktiles::top | win::quicktiles::right) << QRect(640, 0, 640, 512);
    QTest::newRow("bottom left combined") << QStringList{QStringLiteral("Window Quick Tile Left"), QStringLiteral("Window Quick Tile Bottom")} << (win::quicktiles::bottom | win::quicktiles::left) << QRect(0, 512, 640, 512);
    QTest::newRow("bottom right combined") << QStringList{QStringLiteral("Window Quick Tile Right"), QStringLiteral("Window Quick Tile Bottom")} << (win::quicktiles::bottom | win::quicktiles::right) << QRect(640, 512, 640, 512);
#undef FLAG
}

void QuickTilingTest::testShortcut()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Map the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);

    // We have to receive a configure event when the client becomes active.
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QFETCH(QStringList, shortcutList);
    QFETCH(QRect, expectedGeometry);

    const int numberOfQuickTileActions = shortcutList.count();

    if (numberOfQuickTileActions > 1) {
        QTest::qWait(1001);
    }

    for (QString shortcut : shortcutList) {
        // invoke global shortcut through dbus
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kwin"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{shortcut});
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QTRY_COMPARE(quickTileChangedSpy.count(), numberOfQuickTileActions);
    // at this point the geometry did not yet change
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QTEST(c->control->quicktiling(), "expectedMode");

    // but we got requested a new geometry
    QTRY_COMPARE(configureRequestedSpy.count(), numberOfQuickTileActions + 1);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());

    // attach a new image
    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->frameGeometry(), expectedGeometry);
}

void QuickTilingTest::testScript_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<win::quicktiles>("expectedMode");
    QTest::addColumn<QRect>("expectedGeometry");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)
    QTest::newRow("top") << QStringLiteral("Top") << win::quicktiles::top << QRect(0, 0, 1280, 512);
    QTest::newRow("bottom") << QStringLiteral("Bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512);
    QTest::newRow("top right") << QStringLiteral("TopRight") << (win::quicktiles::top | win::quicktiles::right) << QRect(640, 0, 640, 512);
    QTest::newRow("top left") << QStringLiteral("TopLeft") << (win::quicktiles::top | win::quicktiles::left) << QRect(0, 0, 640, 512);
    QTest::newRow("bottom right") << QStringLiteral("BottomRight") << (win::quicktiles::bottom | win::quicktiles::right) << QRect(640, 512, 640, 512);
    QTest::newRow("bottom left") << QStringLiteral("BottomLeft") << (win::quicktiles::bottom | win::quicktiles::left) << QRect(0, 512, 640, 512);
    QTest::newRow("left") << QStringLiteral("Left") << win::quicktiles::left << QRect(0, 0, 640, 1024);
    QTest::newRow("right") << QStringLiteral("Right") << win::quicktiles::right << QRect(640, 0, 640, 1024);
#undef FLAG
}

void QuickTilingTest::testScript()
{
    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellSurface> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Map the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling(), win::quicktiles::none);

    // We have to receive a configure event upon the client becoming active.
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellSurface::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy quickTileChangedSpy(c, &Toplevel::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    QVERIFY(Scripting::self());
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    QTextStream out(&tmpFile);

    QFETCH(QString, action);
    out << "workspace.slotWindowQuickTile" << action << "()";
    out.flush();

    QFETCH(win::quicktiles, expectedMode);
    QFETCH(QRect, expectedGeometry);

    const int id = Scripting::self()->loadScript(tmpFile.fileName());
    QVERIFY(id != -1);
    QVERIFY(Scripting::self()->isScriptLoaded(tmpFile.fileName()));
    auto s = Scripting::self()->findScript(tmpFile.fileName());
    QVERIFY(s);
    QSignalSpy runningChangedSpy(s, &AbstractScript::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    s->run();

    QVERIFY(quickTileChangedSpy.wait());
    QCOMPARE(quickTileChangedSpy.count(), 1);

    QCOMPARE(runningChangedSpy.count(), 1);
    QCOMPARE(runningChangedSpy.first().first().toBool(), true);

    // at this point the geometry did not yet change
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling(), expectedMode);

    // but we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());

    // attach a new image
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->frameGeometry(), expectedGeometry);
}

}

WAYLANDTEST_MAIN(KWin::QuickTilingTest)
#include "quick_tiling_test.moc"
