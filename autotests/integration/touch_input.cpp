/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("touch input", "[input]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("touch-input", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::seat | global_selection::xdg_decoration);

    QVERIFY(wait_for_wayland_touch());
    auto seat = get_client().interfaces.seat.get();
    auto touch = std::unique_ptr<Wrapland::Client::Touch>(seat->createTouch(seat));
    QVERIFY(touch);
    QVERIFY(touch->isValid());

    struct window_holder {
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        std::unique_ptr<Wrapland::Client::Surface> surface;
    };
    std::vector<window_holder> clients;

    auto showWindow = [&](bool decorated = false) -> wayland_window* {
        using namespace Wrapland::Client;

        window_holder client;
        client.surface = create_surface();
        REQUIRE(client.surface.get());
        client.toplevel = create_xdg_shell_toplevel(client.surface, CreationSetup::CreateOnly);
        REQUIRE(client.toplevel.get());

        if (decorated) {
            auto deco = get_client().interfaces.xdg_decoration->getToplevelDecoration(
                client.toplevel.get(), client.toplevel.get());
            QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
            REQUIRE(decoSpy.isValid());
            deco->setMode(XdgDecoration::Mode::ServerSide);
            REQUIRE(deco->mode() == XdgDecoration::Mode::ClientSide);
            init_xdg_shell_toplevel(client.surface, client.toplevel);
            REQUIRE(deco->mode() == XdgDecoration::Mode::ServerSide);
        } else {
            init_xdg_shell_toplevel(client.surface, client.toplevel);
        }

        // let's render
        auto c = render_and_wait_for_shown(client.surface, QSize(100, 50), Qt::blue);

        REQUIRE(c);
        REQUIRE(get_wayland_window(setup.base->space->stacking.active) == c);

        clients.push_back(std::move(client));
        return c;
    };

    SECTION("touch hides cursor")
    {
        QCOMPARE(cursor()->is_hidden(), false);
        quint32 timestamp = 1;
        touch_down(1, QPointF(125, 125), timestamp++);
        QCOMPARE(cursor()->is_hidden(), true);
        touch_down(2, QPointF(130, 125), timestamp++);
        touch_up(2, timestamp++);
        touch_up(1, timestamp++);

        // now a mouse event should show the cursor again
        pointer_motion_absolute(QPointF(0, 0), timestamp++);
        QCOMPARE(cursor()->is_hidden(), false);

        // touch should hide again
        touch_down(1, QPointF(125, 125), timestamp++);
        touch_up(1, timestamp++);
        QCOMPARE(cursor()->is_hidden(), true);

        // wheel should also show
        pointer_axis_vertical(1.0, timestamp++, 0);
        QCOMPARE(cursor()->is_hidden(), false);
    }

    SECTION("multiple touch points")
    {
        using namespace Wrapland::Client;

        auto decorated = GENERATE(false, true);

        auto c = showWindow(decorated);
        REQUIRE((win::decoration(c) != nullptr) == decorated);
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
        touch_down(1, QPointF(125, 125) + win::frame_to_client_pos(c, QPoint()), timestamp++);
        QVERIFY(sequenceStartedSpy.wait());
        QCOMPARE(sequenceStartedSpy.count(), 1);
        QCOMPARE(touch->sequence().count(), 1);
        QCOMPARE(touch->sequence().first()->isDown(), true);
        QCOMPARE(touch->sequence().first()->position(), QPointF(25, 25));
        QCOMPARE(pointAddedSpy.count(), 0);
        QCOMPARE(pointMovedSpy.count(), 0);

        // a point outside the window
        touch_down(2, QPointF(0, 0) + win::frame_to_client_pos(c, QPoint()), timestamp++);
        QVERIFY(pointAddedSpy.wait());
        QCOMPARE(pointAddedSpy.count(), 1);
        QCOMPARE(touch->sequence().count(), 2);
        QCOMPARE(touch->sequence().at(1)->isDown(), true);
        QCOMPARE(touch->sequence().at(1)->position(), QPointF(-100, -100));
        QCOMPARE(pointMovedSpy.count(), 0);

        // let's move that one
        touch_motion(2, QPointF(100, 100) + win::frame_to_client_pos(c, QPoint()), timestamp++);
        QVERIFY(pointMovedSpy.wait());
        QCOMPARE(pointMovedSpy.count(), 1);
        QCOMPARE(touch->sequence().count(), 2);
        QCOMPARE(touch->sequence().at(1)->isDown(), true);
        QCOMPARE(touch->sequence().at(1)->position(), QPointF(0, 0));

        touch_up(1, timestamp++);
        QVERIFY(pointRemovedSpy.wait());
        QCOMPARE(pointRemovedSpy.count(), 1);
        QCOMPARE(touch->sequence().count(), 2);
        QCOMPARE(touch->sequence().first()->isDown(), false);
        QCOMPARE(endedSpy.count(), 0);

        touch_up(2, timestamp++);
        QVERIFY(pointRemovedSpy.wait());
        QCOMPARE(pointRemovedSpy.count(), 2);
        QCOMPARE(touch->sequence().count(), 2);
        QCOMPARE(touch->sequence().first()->isDown(), false);
        QCOMPARE(touch->sequence().at(1)->isDown(), false);
        QCOMPARE(endedSpy.count(), 1);
    }

    SECTION("cancel")
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
        touch_down(1, QPointF(125, 125), timestamp++);
        QVERIFY(sequenceStartedSpy.wait());
        QCOMPARE(sequenceStartedSpy.count(), 1);

        // cancel
        touch_cancel();
        QVERIFY(cancelSpy.wait());
        QCOMPARE(cancelSpy.count(), 1);

        touch_up(1, timestamp++);
        QVERIFY(!pointRemovedSpy.wait(100));
        QCOMPARE(pointRemovedSpy.count(), 0);
    }

    SECTION("touch mouse action")
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
        touch_down(1, c1->geo.frame.center(), timestamp++);
        QVERIFY(c1->control->active);

        QVERIFY(sequenceStartedSpy.wait());
        QCOMPARE(sequenceStartedSpy.count(), 1);

        // cleanup
        touch_cancel();
    }
}

}
