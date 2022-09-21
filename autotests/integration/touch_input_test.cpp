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
#include "toplevel.h"
#include "win/deco.h"
#include "win/move.h"
#include "win/space.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>
#include <Wrapland/Client/xdgdecoration.h>

namespace KWin
{

class TouchInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testTouchHidesCursor();
    void testMultipleTouchPoints_data();
    void testMultipleTouchPoints();
    void testCancel();
    void testTouchMouseAction();

private:
    Test::wayland_window* showWindow(bool decorated = false);

    std::unique_ptr<Wrapland::Client::Touch> touch;

    struct window_holder {
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        std::unique_ptr<Wrapland::Client::Surface> surface;
    };
    std::vector<window_holder> clients;
};

void TouchInputTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TouchInputTest::init()
{
    using namespace Wrapland::Client;
    Test::setup_wayland_connection(Test::global_selection::seat
                                   | Test::global_selection::xdg_decoration);
    QVERIFY(Test::wait_for_wayland_touch());
    auto seat = Test::get_client().interfaces.seat.get();
    touch = std::unique_ptr<Wrapland::Client::Touch>(seat->createTouch(seat));
    QVERIFY(touch);
    QVERIFY(touch->isValid());

    Test::cursor()->set_pos(QPoint(1280, 512));
}

void TouchInputTest::cleanup()
{
    clients.clear();
    touch.reset();
    Test::destroy_wayland_connection();
}

Test::wayland_window* TouchInputTest::showWindow(bool decorated)
{
    using namespace Wrapland::Client;
#define VERIFY(statement)                                                                          \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))                          \
        return nullptr;
#define COMPARE(actual, expected)                                                                  \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))                \
        return nullptr;

    window_holder client;
    client.surface = Test::create_surface();
    VERIFY(client.surface.get());
    client.toplevel
        = Test::create_xdg_shell_toplevel(client.surface, Test::CreationSetup::CreateOnly);
    VERIFY(client.toplevel.get());
    if (decorated) {
        auto deco = Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
            client.toplevel.get(), client.toplevel.get());
        QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
        VERIFY(decoSpy.isValid());
        deco->setMode(XdgDecoration::Mode::ServerSide);
        COMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
        Test::init_xdg_shell_toplevel(client.surface, client.toplevel);
        COMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
    } else {
        Test::init_xdg_shell_toplevel(client.surface, client.toplevel);
    }
    // let's render
    auto c = Test::render_and_wait_for_shown(client.surface, QSize(100, 50), Qt::blue);

    VERIFY(c);
    COMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);

#undef VERIFY
#undef COMPARE

    clients.push_back(std::move(client));
    return c;
}

void TouchInputTest::testTouchHidesCursor()
{
    QCOMPARE(Test::cursor()->is_hidden(), false);
    quint32 timestamp = 1;
    Test::touch_down(1, QPointF(125, 125), timestamp++);
    QCOMPARE(Test::cursor()->is_hidden(), true);
    Test::touch_down(2, QPointF(130, 125), timestamp++);
    Test::touch_up(2, timestamp++);
    Test::touch_up(1, timestamp++);

    // now a mouse event should show the cursor again
    Test::pointer_motion_absolute(QPointF(0, 0), timestamp++);
    QCOMPARE(Test::cursor()->is_hidden(), false);

    // touch should hide again
    Test::touch_down(1, QPointF(125, 125), timestamp++);
    Test::touch_up(1, timestamp++);
    QCOMPARE(Test::cursor()->is_hidden(), true);

    // wheel should also show
    Test::pointer_axis_vertical(1.0, timestamp++, 0);
    QCOMPARE(Test::cursor()->is_hidden(), false);
}

void TouchInputTest::testMultipleTouchPoints_data()
{
    QTest::addColumn<bool>("decorated");

    QTest::newRow("undecorated") << false;
    QTest::newRow("decorated") << true;
}

void TouchInputTest::testMultipleTouchPoints()
{
    using namespace Wrapland::Client;
    QFETCH(bool, decorated);
    auto c = showWindow(decorated);
    QCOMPARE(win::decoration(c) != nullptr, decorated);
    win::move(c, QPoint(100, 100));
    QVERIFY(c);
    QSignalSpy sequenceStartedSpy(touch.get(), &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());
    QSignalSpy pointAddedSpy(touch.get(), &Touch::pointAdded);
    QVERIFY(pointAddedSpy.isValid());
    QSignalSpy pointMovedSpy(touch.get(), &Touch::pointMoved);
    QVERIFY(pointMovedSpy.isValid());
    QSignalSpy pointRemovedSpy(touch.get(), &Touch::pointRemoved);
    QVERIFY(pointRemovedSpy.isValid());
    QSignalSpy endedSpy(touch.get(), &Touch::sequenceEnded);
    QVERIFY(endedSpy.isValid());

    quint32 timestamp = 1;
    Test::touch_down(1, QPointF(125, 125) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);
    QCOMPARE(touch->sequence().count(), 1);
    QCOMPARE(touch->sequence().first()->isDown(), true);
    QCOMPARE(touch->sequence().first()->position(), QPointF(25, 25));
    QCOMPARE(pointAddedSpy.count(), 0);
    QCOMPARE(pointMovedSpy.count(), 0);

    // a point outside the window
    Test::touch_down(2, QPointF(0, 0) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(pointAddedSpy.wait());
    QCOMPARE(pointAddedSpy.count(), 1);
    QCOMPARE(touch->sequence().count(), 2);
    QCOMPARE(touch->sequence().at(1)->isDown(), true);
    QCOMPARE(touch->sequence().at(1)->position(), QPointF(-100, -100));
    QCOMPARE(pointMovedSpy.count(), 0);

    // let's move that one
    Test::touch_motion(2, QPointF(100, 100) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(pointMovedSpy.wait());
    QCOMPARE(pointMovedSpy.count(), 1);
    QCOMPARE(touch->sequence().count(), 2);
    QCOMPARE(touch->sequence().at(1)->isDown(), true);
    QCOMPARE(touch->sequence().at(1)->position(), QPointF(0, 0));

    Test::touch_up(1, timestamp++);
    QVERIFY(pointRemovedSpy.wait());
    QCOMPARE(pointRemovedSpy.count(), 1);
    QCOMPARE(touch->sequence().count(), 2);
    QCOMPARE(touch->sequence().first()->isDown(), false);
    QCOMPARE(endedSpy.count(), 0);

    Test::touch_up(2, timestamp++);
    QVERIFY(pointRemovedSpy.wait());
    QCOMPARE(pointRemovedSpy.count(), 2);
    QCOMPARE(touch->sequence().count(), 2);
    QCOMPARE(touch->sequence().first()->isDown(), false);
    QCOMPARE(touch->sequence().at(1)->isDown(), false);
    QCOMPARE(endedSpy.count(), 1);
}

void TouchInputTest::testCancel()
{
    using namespace Wrapland::Client;
    auto c = showWindow();
    win::move(c, QPoint(100, 100));
    QVERIFY(c);
    QSignalSpy sequenceStartedSpy(touch.get(), &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());
    QSignalSpy cancelSpy(touch.get(), &Touch::sequenceCanceled);
    QVERIFY(cancelSpy.isValid());
    QSignalSpy pointRemovedSpy(touch.get(), &Touch::pointRemoved);
    QVERIFY(pointRemovedSpy.isValid());

    quint32 timestamp = 1;
    Test::touch_down(1, QPointF(125, 125), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);

    // cancel
    Test::touch_cancel();
    QVERIFY(cancelSpy.wait());
    QCOMPARE(cancelSpy.count(), 1);

    Test::touch_up(1, timestamp++);
    QVERIFY(!pointRemovedSpy.wait(100));
    QCOMPARE(pointRemovedSpy.count(), 0);
}

void TouchInputTest::testTouchMouseAction()
{
    // this test verifies that a touch down on an inactive client will activate it
    using namespace Wrapland::Client;
    // create two windows
    auto c1 = showWindow();
    QVERIFY(c1);
    auto c2 = showWindow();
    QVERIFY(c2);

    QVERIFY(!c1->control->active);
    QVERIFY(c2->control->active);

    // also create a sequence started spy as the touch event should be passed through
    QSignalSpy sequenceStartedSpy(touch.get(), &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());

    quint32 timestamp = 1;
    Test::touch_down(1, c1->geo.frame.center(), timestamp++);
    QVERIFY(c1->control->active);

    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);

    // cleanup
    Test::touch_cancel();
}

}

WAYLANDTEST_MAIN(KWin::TouchInputTest)
#include "touch_input_test.moc"
