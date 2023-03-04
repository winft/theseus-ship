/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
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

namespace KWin::detail::test
{

TEST_CASE("input stacking order", "[win]")
{
    test::setup setup("input-stacking-order");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    setup_wayland_connection(global_selection::seat);
    QVERIFY(wait_for_wayland_pointer());
    cursor()->set_pos(QPoint(640, 512));

    auto render = [](std::unique_ptr<Wrapland::Client::Surface> const& surface) {
        test::render(surface, QSize(100, 50), Qt::blue);
        flush_wayland_connection();
    };

    SECTION("pointer focus updates on stacking order change")
    {
        // this test creates two windows which overlap
        // the pointer is in the overlapping area which means the top most window has focus
        // as soon as the top most window gets lowered the window should lose focus and the
        // other window should gain focus without a mouse event in between
        using namespace Wrapland::Client;
        // create pointer and signal spy for enter and leave signals
        auto seat = get_client().interfaces.seat.get();
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());

        // now create the two windows and make them overlap
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface1 = create_surface();
        QVERIFY(surface1);
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(shellSurface1);
        render(surface1);
        QVERIFY(clientAddedSpy.wait());
        auto window1 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window1);

        auto surface2 = create_surface();
        QVERIFY(surface2);
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(shellSurface2);
        render(surface2);
        QVERIFY(clientAddedSpy.wait());

        auto window2 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window2);
        QVERIFY(window1 != window2);

        // now make windows overlap
        win::move(window2, window1->geo.pos());
        QCOMPARE(window1->geo.frame, window2->geo.frame);

        // enter
        pointer_motion_absolute(QPointF(25, 25), 1);
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 1);
        // window 2 should have focus
        QCOMPARE(pointer->enteredSurface(), surface2.get());
        // also on the server
        QCOMPARE(setup.base->server->seat()->pointers().get_focus().surface, window2->surface);

        // raise window 1 above window 2
        QVERIFY(leftSpy.isEmpty());
        win::raise_window(*setup.base->space, window1);

        // should send leave to window2
        QVERIFY(leftSpy.wait());
        QCOMPARE(leftSpy.count(), 1);

        // and an enter to window1
        QCOMPARE(enteredSpy.count(), 2);
        QCOMPARE(pointer->enteredSurface(), surface1.get());
        QCOMPARE(setup.base->server->seat()->pointers().get_focus().surface, window1->surface);

        // let's destroy window1, that should pass focus to window2 again
        QSignalSpy windowClosedSpy(window1->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        surface1.reset();
        QVERIFY(windowClosedSpy.wait());
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 3);
        QCOMPARE(pointer->enteredSurface(), surface2.get());
        QCOMPARE(setup.base->server->seat()->pointers().get_focus().surface, window2->surface);
    }
}

}
