/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/active_window.h"
#include "win/control.h"
#include "win/move.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/pointergestures.h>
#include <Wrapland/Client/surface.h>

namespace KWin::detail::test
{

TEST_CASE("gestures", "[input]")
{
    test::setup setup("gestures");
    setup.start();

    cursor()->set_pos(QPoint(500, 500));
    setup_wayland_connection(global_selection::seat | global_selection::pointer_gestures);
    QVERIFY(wait_for_wayland_pointer());

    SECTION("forward swipe")
    {
        // This test verifies that swipe gestures are correctly forwarded to clients.
        auto& client_gestures = get_client().interfaces.pointer_gestures;

        auto client_pointer = std::unique_ptr<Wrapland::Client::Pointer>(
            get_client().interfaces.seat->createPointer());
        auto client_gesture = std::unique_ptr<Wrapland::Client::PointerSwipeGesture>(
            client_gestures->createSwipeGesture(client_pointer.get()));

        QSignalSpy begin_spy(client_gesture.get(), &Wrapland::Client::PointerSwipeGesture::started);
        QSignalSpy update_spy(client_gesture.get(),
                              &Wrapland::Client::PointerSwipeGesture::updated);
        QSignalSpy end_spy(client_gesture.get(), &Wrapland::Client::PointerSwipeGesture::ended);
        QSignalSpy cancel_spy(client_gesture.get(),
                              &Wrapland::Client::PointerSwipeGesture::cancelled);

        // Arbitrary test values.
        auto fingers = 3;
        auto dx = 1;
        auto dy = 2;
        uint32_t time{0};

        auto surface = create_surface();
        auto toplevel = create_xdg_shell_toplevel(surface);
        auto window = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(window->control->active);

        // Swipes without surface focus aren't forwarded.
        swipe_begin(fingers, ++time);
        QVERIFY(!begin_spy.wait(50));
        QCOMPARE(begin_spy.size(), 0);

        swipe_update(fingers, ++dx, ++dy, ++time);
        QVERIFY(!update_spy.wait(50));
        QCOMPARE(update_spy.size(), 0);

        swipe_end(++time);
        QVERIFY(!end_spy.wait(50));
        QCOMPARE(end_spy.size(), 0);

        cursor()->set_pos(QPoint(10, 10));
        swipe_begin(fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 1);

        swipe_update(fingers, ++dx, ++dy, ++time);
        QVERIFY(update_spy.wait());
        QCOMPARE(update_spy.back().front().toSizeF(), QSizeF(dx, dy));
        QCOMPARE(update_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(update_spy.size(), 1);

        swipe_end(++time);
        QVERIFY(end_spy.wait());
        QCOMPARE(end_spy.back().back().toInt(), time);
        QCOMPARE(end_spy.size(), 1);

        swipe_begin(++fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 2);

        swipe_cancel(++time);
        QVERIFY(cancel_spy.wait());
        QCOMPARE(cancel_spy.back().back().toInt(), time);
        QCOMPARE(cancel_spy.size(), 1);
        QCOMPARE(end_spy.size(), 1);
    }

    SECTION("forward pinch")
    {
        // This test verifies that pinch gestures are correctly forwarded to clients.
        auto& client_gestures = get_client().interfaces.pointer_gestures;

        auto client_pointer = std::unique_ptr<Wrapland::Client::Pointer>(
            get_client().interfaces.seat->createPointer());
        auto client_gesture = std::unique_ptr<Wrapland::Client::PointerPinchGesture>(
            client_gestures->createPinchGesture(client_pointer.get()));

        QSignalSpy begin_spy(client_gesture.get(), &Wrapland::Client::PointerPinchGesture::started);
        QSignalSpy update_spy(client_gesture.get(),
                              &Wrapland::Client::PointerPinchGesture::updated);
        QSignalSpy end_spy(client_gesture.get(), &Wrapland::Client::PointerPinchGesture::ended);
        QSignalSpy cancel_spy(client_gesture.get(),
                              &Wrapland::Client::PointerPinchGesture::cancelled);

        // Arbitrary test values.
        auto fingers = 3;
        auto dx = 1;
        auto dy = 2;
        auto scale = 2;
        auto rotation = 180;
        uint32_t time{0};

        auto surface = create_surface();
        auto toplevel = create_xdg_shell_toplevel(surface);
        auto window = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(window->control->active);

        // Pinches without surface focus aren't forwarded.
        pinch_begin(fingers, ++time);
        QVERIFY(!begin_spy.wait(50));
        QCOMPARE(begin_spy.size(), 0);

        pinch_update(fingers, ++dx, ++dy, ++scale, ++rotation, ++time);
        QVERIFY(!update_spy.wait(50));
        QCOMPARE(update_spy.size(), 0);

        pinch_end(++time);
        QVERIFY(!end_spy.wait(50));
        QCOMPARE(end_spy.size(), 0);

        cursor()->set_pos(QPoint(10, 10));
        pinch_begin(fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 1);

        pinch_update(fingers, ++dx, ++dy, ++scale, ++rotation, ++time);
        QVERIFY(update_spy.wait());
        QCOMPARE(update_spy.back().front().toSizeF(), QSizeF(dx, dy));
        QCOMPARE(update_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(update_spy.size(), 1);

        pinch_end(++time);
        QVERIFY(end_spy.wait());
        QCOMPARE(end_spy.back().back().toInt(), time);
        QCOMPARE(end_spy.size(), 1);

        pinch_begin(++fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 2);

        pinch_cancel(++time);
        QVERIFY(cancel_spy.wait());
        QCOMPARE(cancel_spy.back().back().toInt(), time);
        QCOMPARE(cancel_spy.size(), 1);
        QCOMPARE(end_spy.size(), 1);
    }

    SECTION("forward hold")
    {
        // This test verifies that hold gestures are correctly forwarded to clients.
        auto& client_gestures = get_client().interfaces.pointer_gestures;

        auto client_pointer = std::unique_ptr<Wrapland::Client::Pointer>(
            get_client().interfaces.seat->createPointer());
        auto client_gesture = std::unique_ptr<Wrapland::Client::pointer_hold_gesture>(
            client_gestures->create_hold_gesture(client_pointer.get()));

        QSignalSpy begin_spy(client_gesture.get(),
                             &Wrapland::Client::pointer_hold_gesture::started);
        QSignalSpy end_spy(client_gesture.get(), &Wrapland::Client::pointer_hold_gesture::ended);
        QSignalSpy cancel_spy(client_gesture.get(),
                              &Wrapland::Client::pointer_hold_gesture::cancelled);

        // Arbitrary test values.
        auto fingers = 3;
        uint32_t time{0};

        auto surface = create_surface();
        auto toplevel = create_xdg_shell_toplevel(surface);
        auto window = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(window->control->active);

        // Holds without surface focus aren't forwarded.
        hold_begin(fingers, ++time);
        QVERIFY(!begin_spy.wait(50));
        QCOMPARE(begin_spy.size(), 0);

        hold_end(++time);
        QVERIFY(!end_spy.wait(50));
        QCOMPARE(end_spy.size(), 0);

        cursor()->set_pos(QPoint(10, 10));
        hold_begin(fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 1);

        hold_end(++time);
        QVERIFY(end_spy.wait());
        QCOMPARE(end_spy.back().back().toInt(), time);
        QCOMPARE(end_spy.size(), 1);

        hold_begin(++fingers, ++time);
        QVERIFY(begin_spy.wait());
        QCOMPARE(begin_spy.back().back().toInt(), time);
        QCOMPARE(client_gesture->fingerCount(), fingers);
        QCOMPARE(begin_spy.size(), 2);

        hold_cancel(++time);
        QVERIFY(cancel_spy.wait());
        QCOMPARE(cancel_spy.back().back().toInt(), time);
        QCOMPARE(cancel_spy.size(), 1);
        QCOMPARE(end_spy.size(), 1);
    }
}

}
