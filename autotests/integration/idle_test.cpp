/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include <Wrapland/Client/idle_notify_v1.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/idle_notify_v1.h>

namespace KWin
{

class idle_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_idle();
    void test_activity();

    void test_splice_data();
    void test_splice();
};

void idle_test::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);
    QVERIFY(startup_spy.wait());
}

void idle_test::init()
{
    Test::setup_wayland_connection(Test::global_selection::seat);
}

void idle_test::cleanup()
{
    Test::destroy_wayland_connection();
}

void idle_test::test_idle()
{
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    auto& client = Test::get_client();
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
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);
    QVERIFY(resume_spy.wait());
    QCOMPARE(resume_spy.size(), 1);
    QCOMPARE(idle_spy.size(), 1);

    // Wait for idle one more time.
    QVERIFY(idle_spy.size() == 2 || idle_spy.wait());
    QCOMPARE(idle_spy.size(), 2);
}

void idle_test::test_activity()
{
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    auto& client = Test::get_client();
    auto notification = std::unique_ptr<Wrapland::Client::idle_notification_v1>(
        client.interfaces.idle_notifier->get_notification(2000, client.interfaces.seat.get()));
    QVERIFY(notification->isValid());

    QSignalSpy idle_spy(notification.get(), &Wrapland::Client::idle_notification_v1::idled);
    QVERIFY(idle_spy.isValid());
    QSignalSpy resume_spy(notification.get(), &Wrapland::Client::idle_notification_v1::resumed);
    QVERIFY(resume_spy.isValid());

    // Fake user activity so that idle is never fired. We choose 3*500+1000=2500 > 2000ms.
    uint32_t time{};
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    QTest::qWait(500);
    QVERIFY(idle_spy.empty());

    Test::pointer_button_released(BTN_LEFT, ++time);
    QTest::qWait(500);
    QVERIFY(idle_spy.empty());

    Test::pointer_button_pressed(BTN_LEFT, ++time);
    QTest::qWait(500);

    Test::pointer_button_released(BTN_LEFT, ++time);
    QVERIFY(!idle_spy.wait(1000));
    QVERIFY(idle_spy.empty());

    // Now wait for idle to test the alternative.
    QVERIFY(idle_spy.wait());
    QCOMPARE(idle_spy.size(), 1);

    // Now resume.
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);
    QVERIFY(resume_spy.wait());
    QCOMPARE(resume_spy.size(), 1);
    QCOMPARE(idle_spy.size(), 1);
}

struct notification_wrap {
    notification_wrap(uint32_t duration)
        : interface {
        Test::get_client().interfaces.idle_notifier->get_notification(
            duration,
            Test::get_client().interfaces.seat.get())
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

void idle_test::test_splice_data()
{
    QTest::addColumn<int>("duration1");
    QTest::addColumn<int>("pause");
    QTest::addColumn<int>("duration2");

    QTest::newRow("no-splice-0") << 1000 << 2000 << 0;
    QTest::newRow("no-splice") << 100 << 1000 << 1000;
    QTest::newRow("splice-before") << 1500 << 200 << 100;
    QTest::newRow("splice-before-0") << 1500 << 200 << 0;
    QTest::newRow("splice-after") << 1500 << 200 << 3000;
}

void idle_test::test_splice()
{
    // Verifies that splicing listeners works as expected
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    QFETCH(int, duration1);
    notification_wrap notification1(duration1);

    QFETCH(int, pause);
    QCOMPARE(notification1.idle_spy.wait(pause), pause > duration1);

    QFETCH(int, duration2);
    notification_wrap notification2(duration2);

    // For this test we only allow different values.
    QVERIFY(duration1 != pause + duration2);

    // We chose the durations far enough apart from each other to assure these spy properties.
    if (duration1 < pause + duration2) {
        if (duration1 > pause) {
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
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);

    QVERIFY(notification1.resume_spy.wait());
    QVERIFY(!notification2.resume_spy.empty() || notification2.resume_spy.wait());
    QCOMPARE(notification1.resume_spy.size(), 1);
    QCOMPARE(notification2.resume_spy.size(), 1);

    QVERIFY(duration1 != duration2);

    if (duration1 < duration2) {
        QVERIFY(notification1.idle_spy.wait());
        QVERIFY(notification2.idle_spy.empty());
        QVERIFY(notification2.idle_spy.wait());
    } else {
        // Might already have fired with duration 0.
        QVERIFY(!notification2.idle_spy.empty() || notification2.idle_spy.wait());
        QVERIFY(notification1.idle_spy.empty());
        QVERIFY(notification1.idle_spy.wait());
    }

    QCOMPARE(notification1.idle_spy.size(), 1);
    QCOMPARE(notification2.idle_spy.size(), 1);
}

}

WAYLANDTEST_MAIN(KWin::idle_test)
#include "idle_test.moc"
