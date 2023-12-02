/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/idle_notify_v1.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/idle_notify_v1.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("idle", "[input]")
{
    test::setup setup("idle");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::seat);

    SECTION("idle")
    {
        auto& idle = setup.base->mod.input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        auto& client = get_client();
        auto notification = std::unique_ptr<Wrapland::Client::idle_notification_v1>(
            client.interfaces.idle_notifier->get_notification(1000, client.interfaces.seat.get()));
        QVERIFY(notification->isValid());

        QSignalSpy idle_spy(notification.get(), &Wrapland::Client::idle_notification_v1::idled);
        QVERIFY(idle_spy.isValid());
        QSignalSpy resume_spy(notification.get(), &Wrapland::Client::idle_notification_v1::resumed);
        QVERIFY(resume_spy.isValid());

        // Wait for idle.
        QVERIFY(idle_spy.wait());
        QCOMPARE(idle_spy.size(), 1);

        // Now resume.
        uint32_t time{};
        pointer_button_pressed(BTN_LEFT, ++time);
        pointer_button_released(BTN_LEFT, ++time);
        QVERIFY(resume_spy.wait());
        QCOMPARE(resume_spy.size(), 1);
        QCOMPARE(idle_spy.size(), 1);

        // Wait for idle one more time.
        TRY_REQUIRE(idle_spy.size() == 2);
        QCOMPARE(idle_spy.size(), 2);
    }

    SECTION("activity")
    {
        auto& idle = setup.base->mod.input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        auto& client = get_client();
        auto notification = std::unique_ptr<Wrapland::Client::idle_notification_v1>(
            client.interfaces.idle_notifier->get_notification(2000, client.interfaces.seat.get()));
        QVERIFY(notification->isValid());

        QSignalSpy idle_spy(notification.get(), &Wrapland::Client::idle_notification_v1::idled);
        QVERIFY(idle_spy.isValid());
        QSignalSpy resume_spy(notification.get(), &Wrapland::Client::idle_notification_v1::resumed);
        QVERIFY(resume_spy.isValid());

        // Fake user activity so that idle is never fired. We choose 3*500+1000=2500 > 2000ms.
        uint32_t time{};
        pointer_button_pressed(BTN_LEFT, ++time);
        QTest::qWait(500);
        QVERIFY(idle_spy.empty());

        pointer_button_released(BTN_LEFT, ++time);
        QTest::qWait(500);
        QVERIFY(idle_spy.empty());

        pointer_button_pressed(BTN_LEFT, ++time);
        QTest::qWait(500);

        pointer_button_released(BTN_LEFT, ++time);
        QVERIFY(!idle_spy.wait(1000));
        QVERIFY(idle_spy.empty());

        // Now wait for idle to test the alternative.
        QVERIFY(idle_spy.wait());
        QCOMPARE(idle_spy.size(), 1);

        // Now resume.
        pointer_button_pressed(BTN_LEFT, ++time);
        pointer_button_released(BTN_LEFT, ++time);
        QVERIFY(resume_spy.wait());
        QCOMPARE(resume_spy.size(), 1);
        QCOMPARE(idle_spy.size(), 1);
    }

    SECTION("splice")
    {
        struct notification_wrap {
            notification_wrap(uint32_t duration)
                : interface {
                get_client().interfaces.idle_notifier->get_notification(
                    duration,
                    get_client().interfaces.seat.get())
            }, idle_spy{interface.get(), &Wrapland::Client::idle_notification_v1::idled},
                resume_spy{interface.get(), &Wrapland::Client::idle_notification_v1::resumed}
            {
            }

            void clear_spies()
            {
                idle_spy.clear();
                resume_spy.clear();
            }

            std::unique_ptr<Wrapland::Client::idle_notification_v1> interface;
            QSignalSpy idle_spy;
            QSignalSpy resume_spy;
        };

        struct data {
            int duration1;
            int pause;
            int duration2;
        };

        auto test_data = GENERATE(
            // no splice
            data{1000, 2000, 0},
            data{100, 1000, 1000},
            // splice before
            data{1500, 200, 100},
            data{1500, 200, 0},
            // splice after
            data{1500, 200, 3000});

        // Verifies that splicing listeners works as expected
        auto& idle = setup.base->mod.input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        notification_wrap notification1(test_data.duration1);

        QCOMPARE(notification1.idle_spy.wait(test_data.pause),
                 test_data.pause > test_data.duration1);

        notification_wrap notification2(test_data.duration2);

        // For this test we only allow different values.
        QVERIFY(test_data.duration1 != test_data.pause + test_data.duration2);

        // We chose the durations far enough apart from each other to assure these spy properties.
        if (test_data.duration1 < test_data.pause + test_data.duration2) {
            if (test_data.duration1 > test_data.pause) {
                QVERIFY(notification1.idle_spy.wait());
            } else {
                QVERIFY(!notification1.idle_spy.empty());
            }
            QVERIFY(notification2.idle_spy.empty());
            QVERIFY(notification2.idle_spy.wait());
        } else {
            QVERIFY(notification2.idle_spy.wait());
            QVERIFY(notification1.idle_spy.empty());
            QVERIFY(notification1.idle_spy.wait());
        }

        QCOMPARE(notification1.idle_spy.size(), 1);
        QCOMPARE(notification2.idle_spy.size(), 1);
        QVERIFY(notification1.resume_spy.empty());
        QVERIFY(notification2.resume_spy.empty());

        notification1.clear_spies();
        notification2.clear_spies();

        uint32_t time{};
        pointer_button_pressed(BTN_LEFT, ++time);
        pointer_button_released(BTN_LEFT, ++time);

        QVERIFY(notification1.resume_spy.wait());
        TRY_REQUIRE(notification2.resume_spy.size() == 1);
        QCOMPARE(notification1.resume_spy.size(), 1);
        QCOMPARE(notification2.resume_spy.size(), 1);

        QVERIFY(test_data.duration1 != test_data.duration2);

        if (test_data.duration1 < test_data.duration2) {
            QVERIFY(notification1.idle_spy.wait());
            QVERIFY(notification2.idle_spy.empty());
            QVERIFY(notification2.idle_spy.wait());
        } else {
            // Might already have fired with duration 0.
            TRY_REQUIRE(notification2.idle_spy.size() == 1);
            QVERIFY(notification1.idle_spy.empty());
            QVERIFY(notification1.idle_spy.wait());
        }

        QCOMPARE(notification1.idle_spy.size(), 1);
        QCOMPARE(notification2.idle_spy.size(), 1);
    }
}

}
