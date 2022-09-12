/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include <Wrapland/Client/idle.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/kde_idle.h>

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
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
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
    auto timeout = std::unique_ptr<Wrapland::Client::IdleTimeout>(
        client.interfaces.idle->getTimeout(1000, client.interfaces.seat.get()));
    QVERIFY(timeout->isValid());

    QSignalSpy idle_spy(timeout.get(), &Wrapland::Client::IdleTimeout::idle);
    QVERIFY(idle_spy.isValid());
    QSignalSpy resume_spy(timeout.get(), &Wrapland::Client::IdleTimeout::resumeFromIdle);
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
    auto timeout = std::unique_ptr<Wrapland::Client::IdleTimeout>(
        client.interfaces.idle->getTimeout(2000, client.interfaces.seat.get()));
    QVERIFY(timeout->isValid());

    QSignalSpy idle_spy(timeout.get(), &Wrapland::Client::IdleTimeout::idle);
    QVERIFY(idle_spy.isValid());
    QSignalSpy resume_spy(timeout.get(), &Wrapland::Client::IdleTimeout::resumeFromIdle);
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

struct timeout_wrap {
    timeout_wrap(uint32_t duration)
        : interface {
        Test::get_client().interfaces.idle->getTimeout(duration,
                                                       Test::get_client().interfaces.seat.get())
    }, idle_spy{interface.get(), &Wrapland::Client::IdleTimeout::idle},
        resume_spy{interface.get(), &Wrapland::Client::IdleTimeout::resumeFromIdle}
    {
    }

    void clear_spies()
    {
        idle_spy.clear();
        resume_spy.clear();
    }

    std::unique_ptr<Wrapland::Client::IdleTimeout> interface;
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
    timeout_wrap timeout1(duration1);

    QFETCH(int, pause);
    QCOMPARE(timeout1.idle_spy.wait(pause), pause > duration1);

    QFETCH(int, duration2);
    timeout_wrap timeout2(duration2);

    // For this test we only allow different values.
    QVERIFY(duration1 != pause + duration2);

    // We chose the durations far enough apart from each other to assure these spy properties.
    if (duration1 < pause + duration2) {
        if (duration1 > pause) {
            QVERIFY(timeout1.idle_spy.wait());
        } else {
            QVERIFY(!timeout1.idle_spy.empty());
        }
        QVERIFY(timeout2.idle_spy.empty());
        QVERIFY(timeout2.idle_spy.wait());
    } else {
        QVERIFY(timeout2.idle_spy.wait());
        QVERIFY(timeout1.idle_spy.empty());
        QVERIFY(timeout1.idle_spy.wait());
    }

    QCOMPARE(timeout1.idle_spy.size(), 1);
    QCOMPARE(timeout2.idle_spy.size(), 1);
    QVERIFY(timeout1.resume_spy.empty());
    QVERIFY(timeout2.resume_spy.empty());

    timeout1.clear_spies();
    timeout2.clear_spies();

    uint32_t time{};
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);

    QVERIFY(timeout1.resume_spy.wait());
    QVERIFY(!timeout2.resume_spy.empty() || timeout2.resume_spy.wait());
    QCOMPARE(timeout1.resume_spy.size(), 1);
    QCOMPARE(timeout2.resume_spy.size(), 1);

    QVERIFY(duration1 != duration2);

    if (duration1 < duration2) {
        QVERIFY(timeout1.idle_spy.wait());
        QVERIFY(timeout2.idle_spy.empty());
        QVERIFY(timeout2.idle_spy.wait());
    } else {
        // Might already have fired with duration 0.
        QVERIFY(!timeout2.idle_spy.empty() || timeout2.idle_spy.wait());
        QVERIFY(timeout1.idle_spy.empty());
        QVERIFY(timeout1.idle_spy.wait());
    }

    QCOMPARE(timeout1.idle_spy.size(), 1);
    QCOMPARE(timeout2.idle_spy.size(), 1);
}

}

WAYLANDTEST_MAIN(KWin::idle_test)
#include "idle_test.moc"
