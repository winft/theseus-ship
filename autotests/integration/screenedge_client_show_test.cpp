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
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"

#include <kwineffects/effects_handler.h>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class ScreenEdgeClientShowTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void testScreenEdgeShowHideX11_data();
    void testScreenEdgeShowHideX11();
    void testScreenEdgeShowX11Touch_data();
    void testScreenEdgeShowX11Touch();
};

void ScreenEdgeClientShowTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // set custom config which disable touch edge
    KConfigGroup group = Test::app()->base.config.main->group("TabBox");
    group.writeEntry(QStringLiteral("TouchBorderActivate"), "9");
    group.sync();

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void ScreenEdgeClientShowTest::init()
{
    Test::cursor()->set_pos(QPoint(640, 512));
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

void ScreenEdgeClientShowTest::testScreenEdgeShowHideX11_data()
{
    QTest::addColumn<QRect>("windowGeometry");
    QTest::addColumn<QRect>("resizedWindowGeometry");
    QTest::addColumn<quint32>("location");
    QTest::addColumn<QPoint>("triggerPos");

    QTest::newRow("bottom/left") << QRect(50, 1004, 1180, 20) << QRect(150, 1004, 1000, 20) << 2u
                                 << QPoint(100, 1023);
    QTest::newRow("bottom/right") << QRect(1330, 1004, 1180, 20) << QRect(1410, 1004, 1000, 20)
                                  << 2u << QPoint(1400, 1023);
    QTest::newRow("top/left") << QRect(50, 0, 1180, 20) << QRect(150, 0, 1000, 20) << 0u
                              << QPoint(100, 0);
    QTest::newRow("top/right") << QRect(1330, 0, 1180, 20) << QRect(1410, 0, 1000, 20) << 0u
                               << QPoint(1400, 0);
    QTest::newRow("left") << QRect(0, 10, 20, 1000) << QRect(0, 70, 20, 800) << 3u << QPoint(0, 50);
    QTest::newRow("right") << QRect(2540, 10, 20, 1000) << QRect(2540, 70, 20, 800) << 1u
                           << QPoint(2559, 60);
}

void ScreenEdgeClientShowTest::testScreenEdgeShowHideX11()
{
    // this test creates a window which borders the screen and sets the screenedge show hint
    // that should trigger a show of the window whenever the cursor is pushed against the screen
    // edge

    // create the test window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    // atom for the screenedge show hide functionality
    base::x11::xcb::atom atom(QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

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
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client_id = windowCreatedSpy.first().first().value<quint32>();
    auto client = Test::get_x11_window(Test::app()->base.space->windows_map.at(client_id));
    QVERIFY(client);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->geo.frame, windowGeometry);
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());

    QSignalSpy effectsWindowAdded(effects, &EffectsHandler::windowAdded);
    QVERIFY(effectsWindowAdded.isValid());
    QVERIFY(effectsWindowAdded.wait());

    // now try to hide
    QFETCH(quint32, location);
    xcb_change_property(
        c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &location);
    xcb_flush(c.get());

    QSignalSpy effectsWindowHiddenSpy(effects, &EffectsHandler::windowHidden);
    QVERIFY(effectsWindowHiddenSpy.isValid());
    QSignalSpy clientHiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
    QVERIFY(clientHiddenSpy.isValid());
    QVERIFY(clientHiddenSpy.wait());
    QVERIFY(client->isHiddenInternal());
    QCOMPARE(effectsWindowHiddenSpy.count(), 1);

    // now trigger the edge
    QSignalSpy effectsWindowShownSpy(effects, &EffectsHandler::windowShown);
    QVERIFY(effectsWindowShownSpy.isValid());
    QFETCH(QPoint, triggerPos);
    Test::cursor()->set_pos(triggerPos);
    QVERIFY(!client->isHiddenInternal());
    QCOMPARE(effectsWindowShownSpy.count(), 1);

    // go into event loop to trigger xcb_flush
    QTest::qWait(1);

    // hide window again
    Test::cursor()->set_pos(QPoint(640, 512));
    xcb_change_property(
        c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &location);
    xcb_flush(c.get());
    QVERIFY(clientHiddenSpy.wait());
    QVERIFY(client->isHiddenInternal());
    QFETCH(QRect, resizedWindowGeometry);
    // resizewhile hidden
    client->setFrameGeometry(resizedWindowGeometry);
    // triggerPos shouldn't be valid anymore
    Test::cursor()->set_pos(triggerPos);
    QVERIFY(client->isHiddenInternal());

    // destroy window again
    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

void ScreenEdgeClientShowTest::testScreenEdgeShowX11Touch_data()
{
    QTest::addColumn<QRect>("windowGeometry");
    QTest::addColumn<quint32>("location");
    QTest::addColumn<QPoint>("touchDownPos");
    QTest::addColumn<QPoint>("targetPos");

    QTest::newRow("bottom/left") << QRect(50, 1004, 1180, 20) << 2u << QPoint(100, 1023)
                                 << QPoint(100, 540);
    QTest::newRow("bottom/right") << QRect(1330, 1004, 1180, 20) << 2u << QPoint(1400, 1023)
                                  << QPoint(1400, 520);
    QTest::newRow("top/left") << QRect(50, 0, 1180, 20) << 0u << QPoint(100, 0) << QPoint(100, 350);
    QTest::newRow("top/right") << QRect(1330, 0, 1180, 20) << 0u << QPoint(1400, 0)
                               << QPoint(1400, 400);
    QTest::newRow("left") << QRect(0, 10, 20, 1000) << 3u << QPoint(0, 50) << QPoint(400, 50);
    QTest::newRow("right") << QRect(2540, 10, 20, 1000) << 1u << QPoint(2559, 60)
                           << QPoint(2200, 60);
}

void ScreenEdgeClientShowTest::testScreenEdgeShowX11Touch()
{
    // this test creates a window which borders the screen and sets the screenedge show hint
    // that should trigger a show of the window whenever the touch screen swipe gesture is triggered

    // create the test window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    // atom for the screenedge show hide functionality
    base::x11::xcb::atom atom(QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

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
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client_id = windowCreatedSpy.last().first().value<quint32>();
    auto client = Test::get_x11_window(Test::app()->base.space->windows_map.at(client_id));
    QVERIFY(client);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->geo.frame, windowGeometry);
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());

    QSignalSpy effectsWindowAdded(effects, &EffectsHandler::windowAdded);
    QVERIFY(effectsWindowAdded.isValid());
    QVERIFY(effectsWindowAdded.wait());

    // now try to hide
    QFETCH(quint32, location);
    xcb_change_property(
        c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &location);
    xcb_flush(c.get());

    QSignalSpy effectsWindowHiddenSpy(effects, &EffectsHandler::windowHidden);
    QVERIFY(effectsWindowHiddenSpy.isValid());
    QSignalSpy clientHiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
    QVERIFY(clientHiddenSpy.isValid());
    QVERIFY(clientHiddenSpy.wait());
    QVERIFY(client->isHiddenInternal());
    QCOMPARE(effectsWindowHiddenSpy.count(), 1);

    // now trigger the edge
    QSignalSpy effectsWindowShownSpy(effects, &EffectsHandler::windowShown);
    QVERIFY(effectsWindowShownSpy.isValid());
    quint32 timestamp = 0;
    QFETCH(QPoint, touchDownPos);
    QFETCH(QPoint, targetPos);
    Test::touch_down(0, touchDownPos, timestamp++);
    Test::touch_motion(0, targetPos, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(effectsWindowShownSpy.wait());
    QVERIFY(!client->isHiddenInternal());
    QCOMPARE(effectsWindowShownSpy.count(), 1);

    // destroy window again
    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::ScreenEdgeClientShowTest)
#include "screenedge_client_show_test.moc"
