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
#include "input/pointer_redirect.h"
#include "toplevel.h"
#include "win/deco.h"
#include "win/deco/bridge.h"
#include "win/deco/client_impl.h"
#include "win/deco/settings.h"
#include "win/internal_window.h"
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <linux/input.h>

Q_DECLARE_METATYPE(Qt::WindowFrameSection)

namespace KWin
{

class DecorationInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testAxis_data();
    void testAxis();
    void testDoubleClick_data();
    void testDoubleClick();
    void testDoubleTap_data();
    void testDoubleTap();
    void testHover();
    void testPressToMove_data();
    void testPressToMove();
    void testTapToMove_data();
    void testTapToMove();
    void testResizeOutsideWindow_data();
    void testResizeOutsideWindow();
    void testModifierClickUnrestrictedMove_data();
    void testModifierClickUnrestrictedMove();
    void testModifierScrollOpacity_data();
    void testModifierScrollOpacity();
    void testTouchEvents();
    void testTooltipDoesntEatKeyEvents();

private:
    Test::space::wayland_window* showWindow();

    struct {
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        std::unique_ptr<Wrapland::Client::Surface> surface;
    } client;
};

#define MOTION(target) Test::pointer_motion_absolute(target, timestamp++)

#define PRESS Test::pointer_button_pressed(BTN_LEFT, timestamp++)

#define RELEASE Test::pointer_button_released(BTN_LEFT, timestamp++)

Test::space::wayland_window* DecorationInputTest::showWindow()
{
    using namespace Wrapland::Client;
#define VERIFY(statement)                                                                          \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))                          \
        return nullptr;
#define COMPARE(actual, expected)                                                                  \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))                \
        return nullptr;

    client.surface = Test::create_surface();
    VERIFY(client.surface.get());
    client.toplevel
        = Test::create_xdg_shell_toplevel(client.surface, Test::CreationSetup::CreateOnly);
    VERIFY(client.toplevel.get());

    QSignalSpy configureRequestedSpy(client.toplevel.get(), &XdgShellToplevel::configureRequested);

    auto deco = Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
        client.toplevel.get(), client.toplevel.get());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    VERIFY(decoSpy.isValid());
    deco->setMode(XdgDecoration::Mode::ServerSide);
    COMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(client.surface, client.toplevel);
    COMPARE(decoSpy.count(), 1);
    COMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    VERIFY(configureRequestedSpy.count() > 0 || configureRequestedSpy.wait());
    COMPARE(configureRequestedSpy.count(), 1);

    client.toplevel->ackConfigure(configureRequestedSpy.last()[2].toInt());

    // let's render
    auto c = Test::render_and_wait_for_shown(client.surface, QSize(500, 50), Qt::blue);
    VERIFY(c);
    COMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    COMPARE(c->userCanSetNoBorder(), true);
    COMPARE(win::decoration(c) != nullptr, true);

#undef VERIFY
#undef COMPARE

    return c;
}

void DecorationInputTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // change some options
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group(QStringLiteral("MouseBindings"))
        .writeEntry("CommandTitlebarWheel", QStringLiteral("above/below"));
    config->group(QStringLiteral("Windows"))
        .writeEntry("TitlebarDoubleClickCommand", QStringLiteral("OnAllDesktops"));
    config->group(QStringLiteral("Desktops")).writeEntry("Number", 2);
    config->sync();

    kwinApp()->setConfig(config);

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void DecorationInputTest::init()
{
    using namespace Wrapland::Client;
    Test::setup_wayland_connection(Test::global_selection::seat
                                   | Test::global_selection::xdg_decoration);
    QVERIFY(Test::wait_for_wayland_pointer());

    Test::cursor()->set_pos(QPoint(640, 512));
}

void DecorationInputTest::cleanup()
{
    client = {};
    Test::destroy_wayland_connection();
}

void DecorationInputTest::testAxis_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(0, 0) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(250, 0) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(499, 0) << Qt::TopRightSection;
}

void DecorationInputTest::testAxis()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!c->control->keep_above);
    QVERIFY(!c->control->keep_below);

    quint32 timestamp = 1;

    MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

    QVERIFY(Test::app()->base.space->input->pointer->focus.deco.client);
    QCOMPARE(Test::app()
                 ->base.space->input->pointer->focus.deco.client->decoration()
                 ->sectionUnderMouse(),
             Qt::TitleBarArea);

    // TODO: mouse wheel direction looks wrong to me
    // simulate wheel
    Test::pointer_axis_vertical(5.0, timestamp++, 0);
    QVERIFY(c->control->keep_below);
    QVERIFY(!c->control->keep_above);
    Test::pointer_axis_vertical(-5.0, timestamp++, 0);
    QVERIFY(!c->control->keep_below);
    QVERIFY(!c->control->keep_above);
    Test::pointer_axis_vertical(-5.0, timestamp++, 0);
    QVERIFY(!c->control->keep_below);
    QVERIFY(c->control->keep_above);

    // test top most deco pixel, BUG: 362860
    win::move(c, QPoint(0, 0));
    QFETCH(QPoint, decoPoint);
    MOTION(decoPoint);
    QVERIFY(Test::app()->base.space->input->pointer->focus.deco.client);
    QVERIFY(Test::app()->base.space->input->pointer->focus.deco.window);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->input->pointer->focus.window), c);
    QTEST(Test::app()
              ->base.space->input->pointer->focus.deco.client->decoration()
              ->sectionUnderMouse(),
          "expectedSection");
    Test::pointer_axis_vertical(5.0, timestamp++, 0);
    QVERIFY(!c->control->keep_below);

    QEXPECT_FAIL("topLeft|xdgWmBase", "Button at (0,0;24x24) filters out the event", Continue);
    QVERIFY(!c->control->keep_above);
}

void DecorationInputTest::testDoubleClick_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(0, 0) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(250, 0) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(499, 0) << Qt::TopRightSection;
}

void KWin::DecorationInputTest::testDoubleClick()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!win::on_all_desktops(c));
    quint32 timestamp = 1;
    MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

    // double click
    PRESS;
    RELEASE;
    PRESS;
    RELEASE;
    QVERIFY(win::on_all_desktops(c));
    // double click again
    PRESS;
    RELEASE;
    QVERIFY(win::on_all_desktops(c));
    PRESS;
    RELEASE;
    QVERIFY(!win::on_all_desktops(c));

    // test top most deco pixel, BUG: 362860
    win::move(c, QPoint(0, 0));
    QFETCH(QPoint, decoPoint);
    MOTION(decoPoint);
    QVERIFY(Test::app()->base.space->input->pointer->focus.deco.client);
    QVERIFY(Test::app()->base.space->input->pointer->focus.deco.window);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->input->pointer->focus.window), c);
    QTEST(Test::app()
              ->base.space->input->pointer->focus.deco.client->decoration()
              ->sectionUnderMouse(),
          "expectedSection");
    // double click
    PRESS;
    RELEASE;
    QVERIFY(!win::on_all_desktops(c));
    PRESS;
    RELEASE;
    QVERIFY(win::on_all_desktops(c));
}

void DecorationInputTest::testDoubleTap_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(10, 10) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(260, 10) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(509, 10) << Qt::TopRightSection;
}

void KWin::DecorationInputTest::testDoubleTap()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!win::on_all_desktops(c));
    quint32 timestamp = 1;
    const QPoint tapPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2);

    // double tap
    Test::touch_down(0, tapPoint, timestamp++);
    Test::touch_up(0, timestamp++);
    Test::touch_down(0, tapPoint, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(win::on_all_desktops(c));
    // double tap again
    Test::touch_down(0, tapPoint, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(win::on_all_desktops(c));
    Test::touch_down(0, tapPoint, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(!win::on_all_desktops(c));

    // test top most deco pixel, BUG: 362860
    //
    // Not directly at (0, 0), otherwise ScreenEdgeInputFilter catches
    // event before DecorationEventFilter.
    win::move(c, QPoint(10, 10));
    QFETCH(QPoint, decoPoint);

    // double click
    Test::touch_down(0, decoPoint, timestamp++);
    QVERIFY(Test::app()->base.space->input->touch->focus.deco.client);
    QVERIFY(Test::app()->base.space->input->touch->focus.deco.window);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->input->touch->focus.window), c);
    QTEST(
        Test::app()->base.space->input->touch->focus.deco.client->decoration()->sectionUnderMouse(),
        "expectedSection");
    Test::touch_up(0, timestamp++);
    QVERIFY(!win::on_all_desktops(c));
    Test::touch_down(0, decoPoint, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(win::on_all_desktops(c));
}

void DecorationInputTest::testHover()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());

    // our left border is moved out of the visible area, so move the window to a better place
    win::move(c, QPoint(20, 0));

    quint32 timestamp = 1;
    MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));
    QCOMPARE(c->control->move_resize.cursor, input::cursor_shape(Qt::ArrowCursor));

    // There is a mismatch of the cursor key positions between windows
    // with and without borders (with borders one can move inside a bit and still
    // be on an edge, without not). We should make this consistent in KWin's core.
    //
    // TODO: Test input position with different border sizes.
    // TODO: We should test with the fake decoration to have a fixed test environment.
    auto const hasBorders
        = Test::app()->base.space->deco->settings()->borderSize() != KDecoration2::BorderSize::None;
    auto deviation = [hasBorders] { return hasBorders ? -1 : 0; };

    MOTION(QPoint(c->geo.frame.x(), 0));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeNorthWest));
    MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() / 2, 0));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeNorth));
    MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() - 1, 0));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeNorthEast));
    MOTION(
        QPoint(c->geo.frame.x() + c->geo.frame.width() + deviation(), c->geo.size().height() / 2));
    QCOMPARE(c->control->move_resize.cursor, input::cursor_shape(input::extended_cursor::SizeEast));
    MOTION(
        QPoint(c->geo.frame.x() + c->geo.frame.width() + deviation(), c->geo.size().height() - 1));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeSouthEast));
    MOTION(
        QPoint(c->geo.frame.x() + c->geo.frame.width() / 2, c->geo.size().height() + deviation()));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeSouth));
    MOTION(QPoint(c->geo.frame.x(), c->geo.size().height() + deviation()));
    QCOMPARE(c->control->move_resize.cursor,
             input::cursor_shape(input::extended_cursor::SizeSouthWest));
    MOTION(QPoint(c->geo.frame.x() - 1, c->geo.size().height() / 2));
    QCOMPARE(c->control->move_resize.cursor, input::cursor_shape(input::extended_cursor::SizeWest));

    MOTION(c->geo.frame.center());
    QEXPECT_FAIL("", "Cursor not set back on leave", Continue);
    QCOMPARE(c->control->move_resize.cursor, input::cursor_shape(Qt::ArrowCursor));
}

void DecorationInputTest::testPressToMove_data()
{
    QTest::addColumn<QPoint>("offset");
    QTest::addColumn<QPoint>("offset2");
    QTest::addColumn<QPoint>("offset3");

    QTest::newRow("To right|xdgWmBase") << QPoint(10, 0) << QPoint(20, 0) << QPoint(30, 0);
    QTest::newRow("To left|xdgWmBase") << QPoint(-10, 0) << QPoint(-20, 0) << QPoint(-30, 0);
    QTest::newRow("To bottom|xdgWmBase") << QPoint(0, 10) << QPoint(0, 20) << QPoint(0, 30);
    QTest::newRow("To top|xdgWmBase") << QPoint(0, -10) << QPoint(0, -20) << QPoint(0, -30);
}

void DecorationInputTest::testPressToMove()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c,
              Test::get_output(0)->geometry().center()
                  - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
    QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                   &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    quint32 timestamp = 1;
    MOTION(QPoint(c->geo.frame.center().x(),
                  c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));
    QCOMPARE(c->control->move_resize.cursor, input::cursor_shape(Qt::ArrowCursor));

    PRESS;
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset);
    MOTION(QPoint(c->geo.frame.center().x(),
                  c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
           + offset);
    auto const oldPos = c->geo.pos();
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 1);

    RELEASE;
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QEXPECT_FAIL("", "Just trigger move doesn't move the window", Continue);
    QCOMPARE(c->geo.pos(), oldPos + offset);

    // again
    PRESS;
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset2);
    MOTION(QPoint(c->geo.frame.center().x(),
                  c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
           + offset2);
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 2);
    QFETCH(QPoint, offset3);
    MOTION(QPoint(c->geo.frame.center().x(),
                  c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
           + offset3);

    RELEASE;
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);
    // TODO: the offset should also be included
    QCOMPARE(c->geo.pos(), oldPos + offset2 + offset3);
}

void DecorationInputTest::testTapToMove_data()
{
    QTest::addColumn<QPoint>("offset");
    QTest::addColumn<QPoint>("offset2");
    QTest::addColumn<QPoint>("offset3");

    QTest::newRow("To right|xdgWmBase") << QPoint(10, 0) << QPoint(20, 0) << QPoint(30, 0);
    QTest::newRow("To left|xdgWmBase") << QPoint(-10, 0) << QPoint(-20, 0) << QPoint(-30, 0);
    QTest::newRow("To bottom|xdgWmBase") << QPoint(0, 10) << QPoint(0, 20) << QPoint(0, 30);
    QTest::newRow("To top|xdgWmBase") << QPoint(0, -10) << QPoint(0, -20) << QPoint(0, -30);
}

void DecorationInputTest::testTapToMove()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c,
              Test::get_output(0)->geometry().center()
                  - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
    QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                   &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    quint32 timestamp = 1;
    QPoint p = QPoint(c->geo.frame.center().x(),
                      c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2);

    Test::touch_down(0, p, timestamp++);
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset);
    QCOMPARE(Test::app()->base.space->input->touch->decorationPressId(), 0);
    Test::touch_motion(0, p + offset, timestamp++);
    const QPoint oldPos = c->geo.pos();
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 1);

    Test::touch_up(0, timestamp++);
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QEXPECT_FAIL("", "Just trigger move doesn't move the window", Continue);
    QCOMPARE(c->geo.pos(), oldPos + offset);

    // again
    Test::touch_down(1, p + offset, timestamp++);
    QCOMPARE(Test::app()->base.space->input->touch->decorationPressId(), 1);
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset2);
    Test::touch_motion(1,
                       QPoint(c->geo.frame.center().x(),
                              c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
                           + offset2,
                       timestamp++);
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 2);
    QFETCH(QPoint, offset3);
    Test::touch_motion(1,
                       QPoint(c->geo.frame.center().x(),
                              c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
                           + offset3,
                       timestamp++);

    Test::touch_up(1, timestamp++);
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);
    // TODO: the offset should also be included
    QCOMPARE(c->geo.pos(), oldPos + offset2 + offset3);
}

void DecorationInputTest::testResizeOutsideWindow_data()
{
    QTest::addColumn<Qt::Edge>("edge");
    QTest::addColumn<Qt::CursorShape>("expectedCursor");

    QTest::newRow("left") << Qt::LeftEdge << Qt::SizeHorCursor;
    QTest::newRow("right") << Qt::RightEdge << Qt::SizeHorCursor;
    QTest::newRow("bottom") << Qt::BottomEdge << Qt::SizeVerCursor;
}

void DecorationInputTest::testResizeOutsideWindow()
{
    // this test verifies that one can resize the window outside the decoration with NoSideBorder

    // first adjust config
    kwinApp()
        ->config()
        ->group("org.kde.kdecoration2")
        .writeEntry("BorderSize", QStringLiteral("None"));
    kwinApp()->config()->sync();
    win::space_reconfigure(*Test::app()->base.space);

    // now create window
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c,
              Test::get_output(0)->geometry().center()
                  - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
    QVERIFY(c->geo.frame != win::input_geometry(c));
    QVERIFY(win::input_geometry(c).contains(c->geo.frame));
    QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                   &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());

    // go to border
    quint32 timestamp = 1;
    QFETCH(Qt::Edge, edge);
    switch (edge) {
    case Qt::LeftEdge:
        MOTION(QPoint(c->geo.frame.x() - 1, c->geo.frame.center().y()));
        break;
    case Qt::RightEdge:
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() + 1, c->geo.frame.center().y()));
        break;
    case Qt::BottomEdge:
        MOTION(QPoint(c->geo.frame.center().x(), c->geo.frame.y() + c->geo.frame.height() + 1));
        break;
    default:
        break;
    }
    QVERIFY(!c->geo.frame.contains(Test::cursor()->pos()));

    // pressing should trigger resize
    PRESS;
    QVERIFY(!win::is_resize(c));
    QVERIFY(startMoveResizedSpy.wait());
    QVERIFY(win::is_resize(c));

    RELEASE;
    QVERIFY(!win::is_resize(c));
}

void DecorationInputTest::testModifierClickUnrestrictedMove_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<int>("mouseButton");
    QTest::addColumn<QString>("modKey");
    QTest::addColumn<bool>("capsLock");

    const QString alt = QStringLiteral("Alt");
    const QString meta = QStringLiteral("Meta");

    QTest::newRow("Left Alt + Left Click") << KEY_LEFTALT << BTN_LEFT << alt << false;
    QTest::newRow("Left Alt + Right Click") << KEY_LEFTALT << BTN_RIGHT << alt << false;
    QTest::newRow("Left Alt + Middle Click") << KEY_LEFTALT << BTN_MIDDLE << alt << false;
    QTest::newRow("Right Alt + Left Click") << KEY_RIGHTALT << BTN_LEFT << alt << false;
    QTest::newRow("Right Alt + Right Click") << KEY_RIGHTALT << BTN_RIGHT << alt << false;
    QTest::newRow("Right Alt + Middle Click") << KEY_RIGHTALT << BTN_MIDDLE << alt << false;
    // now everything with meta
    QTest::newRow("Left Meta + Left Click") << KEY_LEFTMETA << BTN_LEFT << meta << false;
    QTest::newRow("Left Meta + Right Click") << KEY_LEFTMETA << BTN_RIGHT << meta << false;
    QTest::newRow("Left Meta + Middle Click") << KEY_LEFTMETA << BTN_MIDDLE << meta << false;
    QTest::newRow("Right Meta + Left Click") << KEY_RIGHTMETA << BTN_LEFT << meta << false;
    QTest::newRow("Right Meta + Right Click") << KEY_RIGHTMETA << BTN_RIGHT << meta << false;
    QTest::newRow("Right Meta + Middle Click") << KEY_RIGHTMETA << BTN_MIDDLE << meta << false;

    // and with capslock
    QTest::newRow("Left Alt + Left Click/CapsLock") << KEY_LEFTALT << BTN_LEFT << alt << true;
    QTest::newRow("Left Alt + Right Click/CapsLock") << KEY_LEFTALT << BTN_RIGHT << alt << true;
    QTest::newRow("Left Alt + Middle Click/CapsLock") << KEY_LEFTALT << BTN_MIDDLE << alt << true;
    QTest::newRow("Right Alt + Left Click/CapsLock") << KEY_RIGHTALT << BTN_LEFT << alt << true;
    QTest::newRow("Right Alt + Right Click/CapsLock") << KEY_RIGHTALT << BTN_RIGHT << alt << true;
    QTest::newRow("Right Alt + Middle Click/CapsLock") << KEY_RIGHTALT << BTN_MIDDLE << alt << true;
    // now everything with meta
    QTest::newRow("Left Meta + Left Click/CapsLock") << KEY_LEFTMETA << BTN_LEFT << meta << true;
    QTest::newRow("Left Meta + Right Click/CapsLock") << KEY_LEFTMETA << BTN_RIGHT << meta << true;
    QTest::newRow("Left Meta + Middle Click/CapsLock")
        << KEY_LEFTMETA << BTN_MIDDLE << meta << true;
    QTest::newRow("Right Meta + Left Click/CapsLock") << KEY_RIGHTMETA << BTN_LEFT << meta << true;
    QTest::newRow("Right Meta + Right Click/CapsLock")
        << KEY_RIGHTMETA << BTN_RIGHT << meta << true;
    QTest::newRow("Right Meta + Middle Click/CapsLock")
        << KEY_RIGHTMETA << BTN_MIDDLE << meta << true;
}

void DecorationInputTest::testModifierClickUnrestrictedMove()
{
    // this test ensures that Alt+mouse button press triggers unrestricted move

    // first modify the config for this run
    QFETCH(QString, modKey);
    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", modKey);
    group.writeEntry("CommandAll1", "Move");
    group.writeEntry("CommandAll2", "Move");
    group.writeEntry("CommandAll3", "Move");
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(kwinApp()->options->qobject->commandAllModifier(),
             modKey == QStringLiteral("Alt") ? Qt::AltModifier : Qt::MetaModifier);
    QCOMPARE(kwinApp()->options->qobject->commandAll1(),
             base::options_qobject::MouseUnrestrictedMove);
    QCOMPARE(kwinApp()->options->qobject->commandAll2(),
             base::options_qobject::MouseUnrestrictedMove);
    QCOMPARE(kwinApp()->options->qobject->commandAll3(),
             base::options_qobject::MouseUnrestrictedMove);

    // create a window
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c,
              Test::get_output(0)->geometry().center()
                  - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
    // move cursor on window
    Test::cursor()->set_pos(
        QPoint(c->geo.frame.center().x(),
               c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));

    // simulate modifier+click
    quint32 timestamp = 1;
    QFETCH(bool, capsLock);
    if (capsLock) {
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    }
    QFETCH(int, modifierKey);
    QFETCH(int, mouseButton);
    Test::keyboard_key_pressed(modifierKey, timestamp++);
    QVERIFY(!win::is_move(c));
    Test::pointer_button_pressed(mouseButton, timestamp++);
    QVERIFY(win::is_move(c));
    // release modifier should not change it
    Test::keyboard_key_released(modifierKey, timestamp++);
    QVERIFY(win::is_move(c));
    // but releasing the key should end move/resize
    Test::pointer_button_released(mouseButton, timestamp++);
    QVERIFY(!win::is_move(c));
    if (capsLock) {
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    }
}

void DecorationInputTest::testModifierScrollOpacity_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<QString>("modKey");
    QTest::addColumn<bool>("capsLock");

    const QString alt = QStringLiteral("Alt");
    const QString meta = QStringLiteral("Meta");

    QTest::newRow("Left Alt") << KEY_LEFTALT << alt << false;
    QTest::newRow("Right Alt") << KEY_RIGHTALT << alt << false;
    QTest::newRow("Left Meta") << KEY_LEFTMETA << meta << false;
    QTest::newRow("Right Meta") << KEY_RIGHTMETA << meta << false;
    QTest::newRow("Left Alt/CapsLock") << KEY_LEFTALT << alt << true;
    QTest::newRow("Right Alt/CapsLock") << KEY_RIGHTALT << alt << true;
    QTest::newRow("Left Meta/CapsLock") << KEY_LEFTMETA << meta << true;
    QTest::newRow("Right Meta/CapsLock") << KEY_RIGHTMETA << meta << true;
}

void DecorationInputTest::testModifierScrollOpacity()
{
    // this test verifies that mod+wheel performs a window operation

    // first modify the config for this run
    QFETCH(QString, modKey);
    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", modKey);
    group.writeEntry("CommandAllWheel", "change opacity");
    group.sync();
    win::space_reconfigure(*Test::app()->base.space);

    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c,
              Test::get_output(0)->geometry().center()
                  - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
    // move cursor on window
    Test::cursor()->set_pos(
        QPoint(c->geo.frame.center().x(),
               c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));
    // set the opacity to 0.5
    c->setOpacity(0.5);
    QCOMPARE(c->opacity(), 0.5);

    // simulate modifier+wheel
    quint32 timestamp = 1;
    QFETCH(bool, capsLock);
    if (capsLock) {
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    }
    QFETCH(int, modifierKey);
    Test::keyboard_key_pressed(modifierKey, timestamp++);
    Test::pointer_axis_vertical(-5, timestamp++, 0);
    QCOMPARE(c->opacity(), 0.6);
    Test::pointer_axis_vertical(5, timestamp++, 0);
    QCOMPARE(c->opacity(), 0.5);
    Test::keyboard_key_released(modifierKey, timestamp++);
    if (capsLock) {
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    }
}

class EventHelper : public QObject
{
    Q_OBJECT
public:
    EventHelper()
        : QObject()
    {
    }
    ~EventHelper() override = default;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched)
        if (event->type() == QEvent::HoverMove) {
            Q_EMIT hoverMove();
        } else if (event->type() == QEvent::HoverLeave) {
            Q_EMIT hoverLeave();
        }
        return false;
    }

Q_SIGNALS:
    void hoverMove();
    void hoverLeave();
};

void DecorationInputTest::testTouchEvents()
{
    // this test verifies that the decoration gets a hover leave event on touch release
    // see BUG 386231
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());

    EventHelper helper;
    win::decoration(c)->installEventFilter(&helper);
    QSignalSpy hoverMoveSpy(&helper, &EventHelper::hoverMove);
    QVERIFY(hoverMoveSpy.isValid());
    QSignalSpy hoverLeaveSpy(&helper, &EventHelper::hoverLeave);
    QVERIFY(hoverLeaveSpy.isValid());

    quint32 timestamp = 1;
    const QPoint tapPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2);

    QVERIFY(!Test::app()->base.space->input->touch->focus.deco.client);
    Test::touch_down(0, tapPoint, timestamp++);
    QVERIFY(Test::app()->base.space->input->touch->focus.deco.client);
    QCOMPARE(Test::app()->base.space->input->touch->focus.deco.client->decoration(),
             win::decoration(c));
    QCOMPARE(hoverMoveSpy.count(), 1);
    QCOMPARE(hoverLeaveSpy.count(), 0);
    Test::touch_up(0, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 1);
    QCOMPARE(hoverLeaveSpy.count(), 1);

    QCOMPARE(win::is_move(c), false);

    // let's check that a hover motion is sent if the pointer is on deco, when touch release
    Test::cursor()->set_pos(tapPoint);
    QCOMPARE(hoverMoveSpy.count(), 2);
    Test::touch_down(0, tapPoint, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 3);
    QCOMPARE(hoverLeaveSpy.count(), 1);
    Test::touch_up(0, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 3);
    QCOMPARE(hoverLeaveSpy.count(), 2);
}

void DecorationInputTest::testTooltipDoesntEatKeyEvents()
{
    // this test verifies that a tooltip on the decoration does not steal key events
    // BUG: 393253

    // first create a keyboard
    auto seat = Test::get_client().interfaces.seat.get();
    auto keyboard = seat->createKeyboard(seat);
    QVERIFY(keyboard);
    QSignalSpy enteredSpy(keyboard, &Wrapland::Client::Keyboard::entered);
    QVERIFY(enteredSpy.isValid());

    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(enteredSpy.wait());

    QSignalSpy keyEvent(keyboard, &Wrapland::Client::Keyboard::keyChanged);
    QVERIFY(keyEvent.isValid());

    QSignalSpy clientAddedSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    c->control->deco.client->requestShowToolTip(QStringLiteral("test"));
    // now we should get an internal window

    QVERIFY(clientAddedSpy.wait());
    auto win_id = clientAddedSpy.first().first().value<quint32>();
    auto internal = Test::get_internal_window(Test::app()->base.space->windows_map.at(win_id));
    QVERIFY(internal);
    QVERIFY(internal->isInternal());
    QVERIFY(internal->internalWindow()->flags().testFlag(Qt::ToolTip));

    // now send a key
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_A, timestamp++);
    QVERIFY(keyEvent.wait());
    Test::keyboard_key_released(KEY_A, timestamp++);
    QVERIFY(keyEvent.wait());

    c->control->deco.client->requestHideToolTip();
    Test::wait_for_destroyed(internal);
}

}

WAYLANDTEST_MAIN(KWin::DecorationInputTest)
#include "decoration_input_test.moc"
