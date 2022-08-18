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
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin
{

class InputStackingOrderTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testPointerFocusUpdatesOnStackingOrderChange();

private:
    void render(std::unique_ptr<Wrapland::Client::Surface> const& surface);
};

void InputStackingOrderTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void InputStackingOrderTest::init()
{
    using namespace Wrapland::Client;
    Test::setup_wayland_connection(Test::global_selection::seat);
    QVERIFY(Test::wait_for_wayland_pointer());

    Test::app()->base.input->cursor->set_pos(QPoint(640, 512));
}

void InputStackingOrderTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void InputStackingOrderTest::render(std::unique_ptr<Wrapland::Client::Surface> const& surface)
{
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
}

void InputStackingOrderTest::testPointerFocusUpdatesOnStackingOrderChange()
{
    // this test creates two windows which overlap
    // the pointer is in the overlapping area which means the top most window has focus
    // as soon as the top most window gets lowered the window should lose focus and the
    // other window should gain focus without a mouse event in between
    using namespace Wrapland::Client;
    // create pointer and signal spy for enter and leave signals
    auto seat = Test::get_client().interfaces.seat.get();
    auto pointer = seat->createPointer(seat);
    QVERIFY(pointer);
    QVERIFY(pointer->isValid());
    QSignalSpy enteredSpy(pointer, &Pointer::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(pointer, &Pointer::left);
    QVERIFY(leftSpy.isValid());

    // now create the two windows and make them overlap
    QSignalSpy clientAddedSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface1 = Test::create_surface();
    QVERIFY(surface1);
    auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
    QVERIFY(shellSurface1);
    render(surface1);
    QVERIFY(clientAddedSpy.wait());
    auto window1 = Test::app()->base.space->active_client;
    QVERIFY(window1);

    auto surface2 = Test::create_surface();
    QVERIFY(surface2);
    auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
    QVERIFY(shellSurface2);
    render(surface2);
    QVERIFY(clientAddedSpy.wait());

    auto window2 = Test::app()->base.space->active_client;
    QVERIFY(window2);
    QVERIFY(window1 != window2);

    // now make windows overlap
    win::move(window2, window1->pos());
    QCOMPARE(window1->frameGeometry(), window2->frameGeometry());

    // enter
    Test::pointer_motion_absolute(QPointF(25, 25), 1);
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.count(), 1);
    // window 2 should have focus
    QCOMPARE(pointer->enteredSurface(), surface2.get());
    // also on the server
    QCOMPARE(waylandServer()->seat()->pointers().get_focus().surface, window2->surface);

    // raise window 1 above window 2
    QVERIFY(leftSpy.isEmpty());
    win::raise_window(Test::app()->base.space.get(), window1);
    // should send leave to window2
    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.count(), 1);
    // and an enter to window1
    QCOMPARE(enteredSpy.count(), 2);
    QCOMPARE(pointer->enteredSurface(), surface1.get());
    QCOMPARE(waylandServer()->seat()->pointers().get_focus().surface, window1->surface);

    // let's destroy window1, that should pass focus to window2 again
    QSignalSpy windowClosedSpy(window1->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    surface1.reset();
    QVERIFY(windowClosedSpy.wait());
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.count(), 3);
    QCOMPARE(pointer->enteredSurface(), surface2.get());
    QCOMPARE(waylandServer()->seat()->pointers().get_focus().surface, window2->surface);
}

}

WAYLANDTEST_MAIN(KWin::InputStackingOrderTest)
#include "input_stacking_order.moc"
