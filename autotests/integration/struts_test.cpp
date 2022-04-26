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
#include "win/geo.h"
#include "win/input.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/surface.h>

#include <KDecoration2/Decoration>

#include <map>
#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class StrutsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testWaylandStruts_data();
    void testWaylandStruts();
    void testMoveWaylandPanel();
    void testWaylandMobilePanel();
    void testX11Struts_data();
    void testX11Struts();
    void test363804();
    void testLeftScreenSmallerBottomAligned();
    void testWindowMoveWithPanelBetweenScreens();

private:
    Wrapland::Client::Compositor* m_compositor = nullptr;
    Wrapland::Client::PlasmaShell* m_plasmaShell = nullptr;
};

void StrutsTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // set custom config which disables the Outline
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config->group("Outline");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    kwinApp()->setConfig(config);

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void StrutsTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::plasma_shell);
    m_compositor = Test::get_client().interfaces.compositor.get();
    m_plasmaShell = Test::get_client().interfaces.plasma_shell.get();
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void StrutsTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

void StrutsTest::testWaylandStruts_data()
{
    QTest::addColumn<QVector<QRect>>("windowGeometries");
    QTest::addColumn<QRect>("screen0Maximized");
    QTest::addColumn<QRect>("screen1Maximized");
    QTest::addColumn<QRect>("workArea");
    QTest::addColumn<QRegion>("restrictedMoveArea");

    QTest::newRow("bottom/0") << QVector<QRect>{QRect(0, 992, 1280, 32)} << QRect(0, 0, 1280, 992)
                              << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 992)
                              << QRegion(0, 992, 1280, 32);
    QTest::newRow("bottom/1") << QVector<QRect>{QRect(1280, 992, 1280, 32)}
                              << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 992)
                              << QRect(0, 0, 2560, 992) << QRegion(1280, 992, 1280, 32);
    QTest::newRow("top/0") << QVector<QRect>{QRect(0, 0, 1280, 32)} << QRect(0, 32, 1280, 992)
                           << QRect(1280, 0, 1280, 1024) << QRect(0, 32, 2560, 992)
                           << QRegion(0, 0, 1280, 32);
    QTest::newRow("top/1") << QVector<QRect>{QRect(1280, 0, 1280, 32)} << QRect(0, 0, 1280, 1024)
                           << QRect(1280, 32, 1280, 992) << QRect(0, 32, 2560, 992)
                           << QRegion(1280, 0, 1280, 32);
    QTest::newRow("left/0") << QVector<QRect>{QRect(0, 0, 32, 1024)} << QRect(32, 0, 1248, 1024)
                            << QRect(1280, 0, 1280, 1024) << QRect(32, 0, 2528, 1024)
                            << QRegion(0, 0, 32, 1024);
    QTest::newRow("left/1") << QVector<QRect>{QRect(1280, 0, 32, 1024)} << QRect(0, 0, 1280, 1024)
                            << QRect(1312, 0, 1248, 1024) << QRect(0, 0, 2560, 1024)
                            << QRegion(1280, 0, 32, 1024);
    QTest::newRow("right/0") << QVector<QRect>{QRect(1248, 0, 32, 1024)} << QRect(0, 0, 1248, 1024)
                             << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
                             << QRegion(1248, 0, 32, 1024);
    QTest::newRow("right/1") << QVector<QRect>{QRect(2528, 0, 32, 1024)} << QRect(0, 0, 1280, 1024)
                             << QRect(1280, 0, 1248, 1024) << QRect(0, 0, 2528, 1024)
                             << QRegion(2528, 0, 32, 1024);

    // same with partial panels not covering the whole area
    QTest::newRow("part bottom/0")
        << QVector<QRect>{QRect(100, 992, 1080, 32)} << QRect(0, 0, 1280, 992)
        << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 992) << QRegion(100, 992, 1080, 32);
    QTest::newRow("part bottom/1")
        << QVector<QRect>{QRect(1380, 992, 1080, 32)} << QRect(0, 0, 1280, 1024)
        << QRect(1280, 0, 1280, 992) << QRect(0, 0, 2560, 992) << QRegion(1380, 992, 1080, 32);
    QTest::newRow("part top/0") << QVector<QRect>{QRect(100, 0, 1080, 32)}
                                << QRect(0, 32, 1280, 992) << QRect(1280, 0, 1280, 1024)
                                << QRect(0, 32, 2560, 992) << QRegion(100, 0, 1080, 32);
    QTest::newRow("part top/1") << QVector<QRect>{QRect(1380, 0, 1080, 32)}
                                << QRect(0, 0, 1280, 1024) << QRect(1280, 32, 1280, 992)
                                << QRect(0, 32, 2560, 992) << QRegion(1380, 0, 1080, 32);
    QTest::newRow("part left/0") << QVector<QRect>{QRect(0, 100, 32, 824)}
                                 << QRect(32, 0, 1248, 1024) << QRect(1280, 0, 1280, 1024)
                                 << QRect(32, 0, 2528, 1024) << QRegion(0, 100, 32, 824);
    QTest::newRow("part left/1") << QVector<QRect>{QRect(1280, 100, 32, 824)}
                                 << QRect(0, 0, 1280, 1024) << QRect(1312, 0, 1248, 1024)
                                 << QRect(0, 0, 2560, 1024) << QRegion(1280, 100, 32, 824);
    QTest::newRow("part right/0") << QVector<QRect>{QRect(1248, 100, 32, 824)}
                                  << QRect(0, 0, 1248, 1024) << QRect(1280, 0, 1280, 1024)
                                  << QRect(0, 0, 2560, 1024) << QRegion(1248, 100, 32, 824);
    QTest::newRow("part right/1") << QVector<QRect>{QRect(2528, 100, 32, 824)}
                                  << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1248, 1024)
                                  << QRect(0, 0, 2528, 1024) << QRegion(2528, 100, 32, 824);

    // multiple panels
    QTest::newRow("two bottom panels")
        << QVector<QRect>{QRect(100, 992, 1080, 32), QRect(1380, 984, 1080, 40)}
        << QRect(0, 0, 1280, 992) << QRect(1280, 0, 1280, 984) << QRect(0, 0, 2560, 984)
        << QRegion(100, 992, 1080, 32).united(QRegion(1380, 984, 1080, 40));
    QTest::newRow("two left panels")
        << QVector<QRect>{QRect(0, 10, 32, 390), QRect(0, 450, 40, 100)} << QRect(40, 0, 1240, 1024)
        << QRect(1280, 0, 1280, 1024) << QRect(40, 0, 2520, 1024)
        << QRegion(0, 10, 32, 390).united(QRegion(0, 450, 40, 100));
}

void StrutsTest::testWaylandStruts()
{
    // this test verifies that struts on Wayland panels are handled correctly
    using namespace Wrapland::Client;

    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));

    // second screen
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(1), 1),
             QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));

    // combined
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->clientArea(FullArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->restrictedMoveArea(-1), QRegion());

    struct client_holder {
        win::wayland::window* window;
        std::unique_ptr<Wrapland::Client::PlasmaShellSurface> plasma_surface;
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        std::unique_ptr<Wrapland::Client::Surface> surface;
    };

    QFETCH(QVector<QRect>, windowGeometries);
    // create the panels
    std::vector<client_holder> clients;
    for (auto it = windowGeometries.constBegin(), end = windowGeometries.constEnd(); it != end;
         it++) {
        auto const windowGeometry = *it;
        auto surface = Test::create_surface();
        auto shellSurface
            = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
        auto plasmaSurface = std::unique_ptr<Wrapland::Client::PlasmaShellSurface>(
            m_plasmaShell->createSurface(surface.get()));
        plasmaSurface->setPosition(windowGeometry.topLeft());
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        Test::init_xdg_shell_toplevel(surface, shellSurface);

        // map the window
        auto c = Test::render_and_wait_for_shown(
            surface, windowGeometry.size(), Qt::red, QImage::Format_RGB32);

        QVERIFY(c);
        QVERIFY(!c->control->active());
        QCOMPARE(c->frameGeometry(), windowGeometry);
        QVERIFY(win::is_dock(c));
        QVERIFY(c->hasStrut());
        clients.push_back(
            {c, std::move(plasmaSurface), std::move(shellSurface), std::move(surface)});
    }

    // some props are independent of struts - those first
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));

    // screen 1
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(1), 1),
             QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));

    // combined
    QCOMPARE(workspace()->clientArea(FullArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));

    // now verify the actual updated client areas
    QTEST(workspace()->clientArea(PlacementArea, outputs.at(0), 1), "screen0Maximized");
    QTEST(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), "screen0Maximized");
    QTEST(workspace()->clientArea(PlacementArea, outputs.at(1), 1), "screen1Maximized");
    QTEST(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), "screen1Maximized");
    QTEST(workspace()->clientArea(WorkArea, outputs.at(0), 1), "workArea");
    QTEST(workspace()->restrictedMoveArea(-1), "restrictedMoveArea");

    // delete all surfaces
    for (auto& client : clients) {
        QSignalSpy destroyedSpy(client.window, &QObject::destroyed);
        QVERIFY(destroyedSpy.isValid());
        client = {};
        QVERIFY(destroyedSpy.wait());
    }
    QCOMPARE(workspace()->restrictedMoveArea(-1), QRegion());
}

void StrutsTest::testMoveWaylandPanel()
{
    // this test verifies that repositioning a Wayland panel updates the client area
    using namespace Wrapland::Client;
    const QRect windowGeometry(0, 1000, 1280, 24);
    auto surface = Test::create_surface();
    auto shellSurface = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    plasmaSurface->setPosition(windowGeometry.topLeft());
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    Test::init_xdg_shell_toplevel(surface, shellSurface);

    // map the window
    auto c = Test::render_and_wait_for_shown(
        surface, windowGeometry.size(), Qt::red, QImage::Format_RGB32);
    QVERIFY(c);
    QVERIFY(!c->control->active());
    QCOMPARE(c->frameGeometry(), windowGeometry);
    QVERIFY(win::is_dock(c));
    QVERIFY(c->hasStrut());

    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1000));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 0, 1280, 1000));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 2560, 1000));

    QSignalSpy geometryChangedSpy(c, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    plasmaSurface->setPosition(QPoint(1280, 1000));
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(c->frameGeometry(), QRect(1280, 1000, 1280, 24));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1000));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1000));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 2560, 1000));
}

void StrutsTest::testWaylandMobilePanel()
{
    using namespace Wrapland::Client;

    // First enable maxmizing policy
    KConfigGroup group = kwinApp()->config()->group("Windows");
    group.writeEntry("Placement", "maximizing");
    group.sync();
    workspace()->slotReconfigure();

    // create first top panel
    const QRect windowGeometry(0, 0, 1280, 60);
    auto surface = Test::create_surface();
    auto shellSurface = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(m_plasmaShell->createSurface(surface.get()));
    plasmaSurface->setPosition(windowGeometry.topLeft());
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    Test::init_xdg_shell_toplevel(surface, shellSurface);

    // map the first panel
    auto c = Test::render_and_wait_for_shown(
        surface, windowGeometry.size(), Qt::red, QImage::Format_RGB32);
    QVERIFY(c);
    QVERIFY(!c->control->active());
    QCOMPARE(c->frameGeometry(), windowGeometry);
    QVERIFY(win::is_dock(c));
    QVERIFY(c->hasStrut());

    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 60, 1280, 964));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 60, 1280, 964));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 60, 2560, 964));

    // create another bottom panel
    const QRect windowGeometry2(0, 874, 1280, 150);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(
        Test::create_xdg_shell_toplevel(surface2, Test::CreationSetup::CreateOnly));
    std::unique_ptr<PlasmaShellSurface> plasmaSurface2(
        m_plasmaShell->createSurface(surface2.get()));
    plasmaSurface2->setPosition(windowGeometry2.topLeft());
    plasmaSurface2->setRole(PlasmaShellSurface::Role::Panel);
    Test::init_xdg_shell_toplevel(surface2, shellSurface2);

    auto c1 = Test::render_and_wait_for_shown(
        surface2, windowGeometry2.size(), Qt::blue, QImage::Format_RGB32);

    QVERIFY(c1);
    QVERIFY(!c1->control->active());
    QCOMPARE(c1->frameGeometry(), windowGeometry2);
    QVERIFY(win::is_dock(c1));
    QVERIFY(c1->hasStrut());

    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 60, 1280, 814));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 60, 1280, 814));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 60, 2560, 814));

    // Destroy test clients.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(c1));
}

void StrutsTest::testX11Struts_data()
{
    QTest::addColumn<QRect>("windowGeometry");
    QTest::addColumn<int>("leftStrut");
    QTest::addColumn<int>("rightStrut");
    QTest::addColumn<int>("topStrut");
    QTest::addColumn<int>("bottomStrut");
    QTest::addColumn<int>("leftStrutStart");
    QTest::addColumn<int>("leftStrutEnd");
    QTest::addColumn<int>("rightStrutStart");
    QTest::addColumn<int>("rightStrutEnd");
    QTest::addColumn<int>("topStrutStart");
    QTest::addColumn<int>("topStrutEnd");
    QTest::addColumn<int>("bottomStrutStart");
    QTest::addColumn<int>("bottomStrutEnd");
    QTest::addColumn<QRect>("screen0Maximized");
    QTest::addColumn<QRect>("screen1Maximized");
    QTest::addColumn<QRect>("workArea");
    QTest::addColumn<QRegion>("restrictedMoveArea");

    QTest::newRow("bottom panel/no strut")
        << QRect(0, 980, 1280, 44) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("bottom panel/strut")
        << QRect(0, 980, 1280, 44) << 0 << 0 << 0 << 44 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 1279
        << QRect(0, 0, 1280, 980) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 980)
        << QRegion(0, 980, 1279, 44);
    QTest::newRow("top panel/no strut")
        << QRect(0, 0, 1280, 44) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("top panel/strut")
        << QRect(0, 0, 1280, 44) << 0 << 0 << 44 << 0 << 0 << 0 << 0 << 0 << 0 << 1279 << 0 << 0
        << QRect(0, 44, 1280, 980) << QRect(1280, 0, 1280, 1024) << QRect(0, 44, 2560, 980)
        << QRegion(0, 0, 1279, 44);
    QTest::newRow("left panel/no strut")
        << QRect(0, 0, 60, 1024) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("left panel/strut")
        << QRect(0, 0, 60, 1024) << 60 << 0 << 0 << 0 << 0 << 1023 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(60, 0, 1220, 1024) << QRect(1280, 0, 1280, 1024) << QRect(60, 0, 2500, 1024)
        << QRegion(0, 0, 60, 1023);
    QTest::newRow("right panel/no strut")
        << QRect(1220, 0, 60, 1024) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("right panel/strut")
        << QRect(1220, 0, 60, 1024) << 0 << 1340 << 0 << 0 << 0 << 0 << 0 << 1023 << 0 << 0 << 0
        << 0 << QRect(0, 0, 1220, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion(1220, 0, 60, 1023);
    // second screen
    QTest::newRow("bottom panel 1/no strut")
        << QRect(1280, 980, 1280, 44) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("bottom panel 1/strut")
        << QRect(1280, 980, 1280, 44) << 0 << 0 << 0 << 44 << 0 << 0 << 0 << 0 << 0 << 0 << 1280
        << 2559 << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 980) << QRect(0, 0, 2560, 980)
        << QRegion(1280, 980, 1279, 44);
    QTest::newRow("top panel 1/no strut")
        << QRect(1280, 0, 1280, 44) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("top panel 1 /strut")
        << QRect(1280, 0, 1280, 44) << 0 << 0 << 44 << 0 << 0 << 0 << 0 << 0 << 1280 << 2559 << 0
        << 0 << QRect(0, 0, 1280, 1024) << QRect(1280, 44, 1280, 980) << QRect(0, 44, 2560, 980)
        << QRegion(1280, 0, 1279, 44);
    QTest::newRow("left panel 1/no strut")
        << QRect(1280, 0, 60, 1024) << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion();
    QTest::newRow("left panel 1/strut")
        << QRect(1280, 0, 60, 1024) << 1340 << 0 << 0 << 0 << 0 << 1023 << 0 << 0 << 0 << 0 << 0
        << 0 << QRect(0, 0, 1280, 1024) << QRect(1340, 0, 1220, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion(1280, 0, 60, 1023);
    // invalid struts
    QTest::newRow("bottom panel/ invalid strut")
        << QRect(0, 980, 1280, 44) << 1280 << 0 << 0 << 44 << 980 << 1024 << 0 << 0 << 0 << 0 << 0
        << 1279 << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion(0, 980, 1280, 44);
    QTest::newRow("top panel/ invalid strut")
        << QRect(0, 0, 1280, 44) << 1280 << 0 << 44 << 0 << 0 << 44 << 0 << 0 << 0 << 1279 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion(0, 0, 1280, 44);
    QTest::newRow("top panel/invalid strut 2")
        << QRect(0, 0, 1280, 44) << 0 << 0 << 1024 << 0 << 0 << 0 << 0 << 0 << 0 << 1279 << 0 << 0
        << QRect(0, 0, 1280, 1024) << QRect(1280, 0, 1280, 1024) << QRect(0, 0, 2560, 1024)
        << QRegion(0, 0, 1279, 1024);
}

void StrutsTest::testX11Struts()
{
    // this test verifies that struts are applied correctly for X11 windows

    // no, struts yet
    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));

    // second screen
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(1), 1),
             QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));

    // combined
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->clientArea(FullArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->restrictedMoveArea(-1), QRegion());

    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    QFETCH(QRect, windowGeometry);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Dock);
    // set the extended strut
    QFETCH(int, leftStrut);
    QFETCH(int, rightStrut);
    QFETCH(int, topStrut);
    QFETCH(int, bottomStrut);
    QFETCH(int, leftStrutStart);
    QFETCH(int, leftStrutEnd);
    QFETCH(int, rightStrutStart);
    QFETCH(int, rightStrutEnd);
    QFETCH(int, topStrutStart);
    QFETCH(int, topStrutEnd);
    QFETCH(int, bottomStrutStart);
    QFETCH(int, bottomStrutEnd);
    NETExtendedStrut strut;
    strut.left_start = leftStrutStart;
    strut.left_end = leftStrutEnd;
    strut.left_width = leftStrut;
    strut.right_start = rightStrutStart;
    strut.right_end = rightStrutEnd;
    strut.right_width = rightStrut;
    strut.top_start = topStrutStart;
    strut.top_end = topStrutEnd;
    strut.top_width = topStrut;
    strut.bottom_start = bottomStrutStart;
    strut.bottom_end = bottomStrutEnd;
    strut.bottom_width = bottomStrut;
    info.setExtendedStrut(strut);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), NET::Dock);
    QCOMPARE(client->frameGeometry(), windowGeometry);

    // this should have affected the client area
    // some props are independent of struts - those first
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));

    // screen 1
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(1), 1),
             QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));

    // combined
    QCOMPARE(workspace()->clientArea(FullArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));

    // now verify the actual updated client areas
    QTEST(workspace()->clientArea(PlacementArea, outputs.at(0), 1), "screen0Maximized");
    QTEST(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), "screen0Maximized");
    QTEST(workspace()->clientArea(PlacementArea, outputs.at(1), 1), "screen1Maximized");
    QTEST(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), "screen1Maximized");
    QTEST(workspace()->clientArea(WorkArea, outputs.at(0), 1), "workArea");
    QTEST(workspace()->restrictedMoveArea(-1), "restrictedMoveArea");

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());

    // now struts should be removed again
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(0), 1), QRect(0, 0, 1280, 1024));

    // second screen
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MovementArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(MaximizeFullArea, outputs.at(1), 1),
             QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(FullScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));
    QCOMPARE(workspace()->clientArea(ScreenArea, outputs.at(1), 1), QRect(1280, 0, 1280, 1024));

    // combined
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->clientArea(FullArea, outputs.at(0), 1), QRect(0, 0, 2560, 1024));
    QCOMPARE(workspace()->restrictedMoveArea(-1), QRegion());
}

void StrutsTest::test363804()
{
    // this test verifies the condition described in BUG 363804
    // two screens in a vertical setup, aligned to right border with panel on the bottom screen
    auto const geometries = std::vector<QRect>{{0, 0, 1920, 1080}, {554, 1080, 1366, 768}};
    Test::app()->set_outputs(geometries);
    QCOMPARE(Test::get_output(0)->geometry(), geometries.at(0));
    QCOMPARE(Test::get_output(1)->geometry(), geometries.at(1));
    QCOMPARE(Test::app()->base.topology.size, QSize(1920, 1848));

    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry(554, 1812, 1366, 36);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Dock);
    NETExtendedStrut strut;
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
    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), NET::Dock);
    QCOMPARE(client->frameGeometry(), windowGeometry);

    // now verify the actual updated client areas
    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), geometries.at(0));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), geometries.at(0));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(554, 1080, 1366, 732));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(554, 1080, 1366, 732));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 1920, 1812));

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void StrutsTest::testLeftScreenSmallerBottomAligned()
{
    // this test verifies a two screen setup with the left screen smaller than the right and bottom
    // aligned the panel is on the top of the left screen, thus not at 0/0 what this test in
    // addition tests is whether a window larger than the left screen is not placed into the dead
    // area
    auto const geometries = std::vector<QRect>{{0, 282, 1366, 768}, {1366, 0, 1680, 1050}};
    Test::app()->set_outputs(geometries);
    QCOMPARE(Test::get_output(0)->geometry(), geometries.at(0));
    QCOMPARE(Test::get_output(1)->geometry(), geometries.at(1));
    QCOMPARE(Test::app()->base.topology.size, QSize(3046, 1050));

    // create the panel
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry(0, 282, 1366, 24);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Dock);
    NETExtendedStrut strut;
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
    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), NET::Dock);
    QCOMPARE(client->frameGeometry(), windowGeometry);

    // now verify the actual updated client areas
    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 306, 1366, 744));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 306, 1366, 744));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), geometries.at(1));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), geometries.at(1));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 3046, 1050));

    // now create a window which is larger than screen 0

    xcb_window_t w2 = xcb_generate_id(c.get());
    const QRect windowGeometry2(0, 26, 1366, 2000);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w2,
                      rootWindow(),
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

    auto client2 = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client2);
    QVERIFY(client2 != client);
    QVERIFY(win::decoration(client2));

    QCOMPARE(client2->frameGeometry(), QRect(0, 306, 1366, 744));
    QCOMPARE(client2->maximizeMode(), win::maximize_mode::full);

    // destroy window again
    QSignalSpy normalWindowClosedSpy(client2, &win::x11::window::closed);
    QVERIFY(normalWindowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w2);
    xcb_destroy_window(c.get(), w2);
    xcb_flush(c.get());
    QVERIFY(normalWindowClosedSpy.wait());

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QVERIFY(windowClosedSpy.wait());
}

void StrutsTest::testWindowMoveWithPanelBetweenScreens()
{
    // this test verifies the condition of BUG
    // when moving a window with decorations in a restricted way it should pass from one screen
    // to the other even if there is a panel in between.

    // left screen must be smaller than right screen
    auto const geometries = std::vector<QRect>{{0, 282, 1366, 768}, {1366, 0, 1680, 1050}};
    Test::app()->set_outputs(geometries);
    QCOMPARE(Test::get_output(0)->geometry(), geometries.at(0));
    QCOMPARE(Test::get_output(1)->geometry(), geometries.at(1));
    QCOMPARE(Test::app()->base.topology.size, QSize(3046, 1050));

    // create the panel on the right screen, left edge
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry(1366, 0, 24, 1050);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Dock);
    NETExtendedStrut strut;
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
    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), NET::Dock);
    QCOMPARE(client->frameGeometry(), windowGeometry);

    // now verify the actual updated client areas
    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(0), 1), QRect(0, 282, 1366, 768));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(0), 1), QRect(0, 282, 1366, 768));
    QCOMPARE(workspace()->clientArea(PlacementArea, outputs.at(1), 1), QRect(1390, 0, 1656, 1050));
    QCOMPARE(workspace()->clientArea(MaximizeArea, outputs.at(1), 1), QRect(1390, 0, 1656, 1050));
    QCOMPARE(workspace()->clientArea(WorkArea, outputs.at(0), 1), QRect(0, 0, 3046, 1050));
    QCOMPARE(workspace()->restrictedMoveArea(-1), QRegion(1366, 0, 24, 1050));

    // create another window and try to move it
    xcb_window_t w2 = xcb_generate_id(c.get());
    const QRect windowGeometry2(1500, 400, 200, 300);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w2,
                      rootWindow(),
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

    auto client2 = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client2);
    QVERIFY(client2 != client);
    QVERIFY(win::decoration(client2));
    QCOMPARE(win::frame_to_client_size(client2, client2->size()), QSize(200, 300));
    QCOMPARE(client2->pos(),
             QPoint(1500, 400) - QPoint(win::left_border(client2), win::top_border(client2)));

    const QRect origGeo = client2->frameGeometry();
    input::get_cursor()->set_pos(origGeo.center());
    workspace()->performWindowOperation(client2, base::options::MoveOp);

    QTRY_COMPARE(workspace()->moveResizeClient(), client2);
    QVERIFY(win::is_move(client2));

    // move to next screen - step is 8 pixel, so 800 pixel
    for (int i = 0; i < 100; i++) {
        win::key_press_event(client2, Qt::Key_Left);
        QTest::qWait(10);
    }

    win::key_press_event(client2, Qt::Key_Enter);
    QCOMPARE(win::is_move(client2), false);
    QVERIFY(workspace()->moveResizeClient() == nullptr);
    QCOMPARE(client2->frameGeometry(), QRect(origGeo.translated(-800, 0)));

    // Destroy window again.
    QSignalSpy normalWindowClosedSpy(client2, &win::x11::window::closed);
    QVERIFY(normalWindowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w2);
    xcb_destroy_window(c.get(), w2);
    xcb_flush(c.get());
    QVERIFY(normalWindowClosedSpy.wait());

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::StrutsTest)
#include "struts_test.moc"
