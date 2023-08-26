/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/catch_macros.h"

#include <render/effect/interface/time_line.h>

#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

namespace KWin::detail::test
{

TEST_CASE("timeline", "[effect],[unit]")
{
    SECTION("update forward")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);

        // 0/1000
        timeLine.advance(0ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.0, 0.0001));
        QVERIFY(!timeLine.done());

        // 100/1000
        timeLine.advance(100ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.1, 0.0001));
        QVERIFY(!timeLine.done());

        // 400/1000
        timeLine.advance(400ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.4, 0.0001));
        QVERIFY(!timeLine.done());

        // 900/1000
        timeLine.advance(900ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.9, 0.0001));
        QVERIFY(!timeLine.done());

        // 1000/1000
        timeLine.advance(3000ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(1.0, 0.0001));
        QVERIFY(timeLine.done());
    }

    SECTION("update backward")
    {
        TimeLine timeLine(1000ms, TimeLine::Backward);
        timeLine.setEasingCurve(QEasingCurve::Linear);

        // 0/1000
        timeLine.advance(0ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(1.0, 0.0001));
        QVERIFY(!timeLine.done());

        // 100/1000
        timeLine.advance(100ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.9, 0.0001));
        QVERIFY(!timeLine.done());

        // 400/1000
        timeLine.advance(400ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.6, 0.0001));
        QVERIFY(!timeLine.done());

        // 900/1000
        timeLine.advance(900ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.1, 0.0001));
        QVERIFY(!timeLine.done());

        // 1000/1000
        timeLine.advance(3000ms);
        REQUIRE_THAT(timeLine.value(), Catch::Matchers::WithinAbs(0.0, 0.0001));
        QVERIFY(timeLine.done());
    }

    SECTION("update finished")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.advance(0ms);
        timeLine.setEasingCurve(QEasingCurve::Linear);

        timeLine.advance(1000ms);
        QCOMPARE(timeLine.value(), 1.0);
        QVERIFY(timeLine.done());

        timeLine.advance(1042ms);
        QCOMPARE(timeLine.value(), 1.0);
        QVERIFY(timeLine.done());
    }

    SECTION("toggle direction")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);

        timeLine.advance(0ms);
        QCOMPARE(timeLine.value(), 0.0);
        QVERIFY(!timeLine.done());

        timeLine.advance(600ms);
        QCOMPARE(timeLine.value(), 0.6);
        QVERIFY(!timeLine.done());

        timeLine.toggleDirection();
        QCOMPARE(timeLine.value(), 0.6);
        QVERIFY(!timeLine.done());

        timeLine.advance(800ms);
        QCOMPARE(timeLine.value(), 0.4);
        QVERIFY(!timeLine.done());

        timeLine.advance(3000ms);
        QCOMPARE(timeLine.value(), 0.0);
        QVERIFY(timeLine.done());
    }

    SECTION("reset")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.advance(0ms);

        timeLine.advance(1000ms);
        QCOMPARE(timeLine.value(), 1.0);
        QVERIFY(timeLine.done());

        timeLine.reset();
        QCOMPARE(timeLine.value(), 0.0);
        QVERIFY(!timeLine.done());
    }

    SECTION("set elapsed")
    {
        struct data {
            std::chrono::milliseconds duration;
            std::chrono::milliseconds elapsed;
            std::chrono::milliseconds expected_elapsed;
            bool expected_done;
            bool initially_done;
        };

        auto test_data = GENERATE(data{1000ms, 300ms, 300ms, false, false},
                                  data{1000ms, 300ms, 300ms, false, true},
                                  data{1000ms, 3000ms, 1000ms, true, false},
                                  data{1000ms, 3000ms, 1000ms, true, true},
                                  data{1000ms, 1000ms, 1000ms, true, false},
                                  data{1000ms, 1000ms, 1000ms, true, true});

        TimeLine timeLine(test_data.duration, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.advance(0ms);

        if (test_data.initially_done) {
            timeLine.advance(test_data.duration);
            QVERIFY(timeLine.done());
        }

        timeLine.setElapsed(test_data.elapsed);
        QCOMPARE(timeLine.elapsed(), test_data.expected_elapsed);
        QCOMPARE(timeLine.done(), test_data.expected_done);
    }

    SECTION("set duration")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);

        QCOMPARE(timeLine.duration(), 1000ms);

        timeLine.setDuration(3000ms);
        QCOMPARE(timeLine.duration(), 3000ms);
    }

    SECTION("set duration retargeting")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.advance(0ms);

        timeLine.advance(500ms);
        QCOMPARE(timeLine.value(), 0.5);
        QVERIFY(!timeLine.done());

        timeLine.setDuration(3000ms);
        QCOMPARE(timeLine.value(), 0.5);
        QVERIFY(!timeLine.done());
    }

    SECTION("set duration retargeting small duration")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.advance(0ms);

        timeLine.advance(999ms);
        QCOMPARE(timeLine.value(), 0.999);
        QVERIFY(!timeLine.done());

        timeLine.setDuration(3ms);
        QCOMPARE(timeLine.value(), 1.0);
        QVERIFY(timeLine.done());
    }

    SECTION("running")
    {
        TimeLine timeLine(1000ms, TimeLine::Forward);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.advance(0ms);

        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.advance(100ms);
        QVERIFY(timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.advance(1000ms);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());
    }

    SECTION("strict redirect source mode")
    {
        struct data {
            TimeLine::Direction initial_dir;
            double initial_val;
            TimeLine::Direction final_dir;
            double final_val;
        };

        auto test_data = GENERATE(data{TimeLine::Forward, 0.0, TimeLine::Backward, 0.0},
                                  data{TimeLine::Backward, 1.0, TimeLine::Forward, 1.0});

        TimeLine timeLine(1000ms, test_data.initial_dir);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.setSourceRedirectMode(TimeLine::RedirectMode::Strict);

        REQUIRE(timeLine.direction() == test_data.initial_dir);
        REQUIRE(timeLine.value() == test_data.initial_val);
        QCOMPARE(timeLine.sourceRedirectMode(), TimeLine::RedirectMode::Strict);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.setDirection(test_data.final_dir);
        REQUIRE(timeLine.direction() == test_data.final_dir);
        REQUIRE(timeLine.value() == test_data.final_val);
        QCOMPARE(timeLine.sourceRedirectMode(), TimeLine::RedirectMode::Strict);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());
    }

    SECTION("relaxed redirect source mode")
    {
        struct data {
            TimeLine::Direction initial_dir;
            double initial_val;
            TimeLine::Direction final_dir;
            double final_val;
        };

        auto test_data = GENERATE(data{TimeLine::Forward, 0.0, TimeLine::Backward, 1.0},
                                  data{TimeLine::Backward, 1.0, TimeLine::Forward, 0.0});

        TimeLine timeLine(1000ms, test_data.initial_dir);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.setSourceRedirectMode(TimeLine::RedirectMode::Relaxed);

        REQUIRE(timeLine.direction() == test_data.initial_dir);
        REQUIRE(timeLine.value() == test_data.initial_val);
        QCOMPARE(timeLine.sourceRedirectMode(), TimeLine::RedirectMode::Relaxed);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.setDirection(test_data.final_dir);
        REQUIRE(timeLine.direction() == test_data.final_dir);
        REQUIRE(timeLine.value() == test_data.final_val);
        QCOMPARE(timeLine.sourceRedirectMode(), TimeLine::RedirectMode::Relaxed);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());
    }

    SECTION("strict redirect target mode")
    {
        struct data {
            TimeLine::Direction initial_dir;
            double initial_val;
            TimeLine::Direction final_dir;
            double final_val;
        };

        auto test_data = GENERATE(data{TimeLine::Forward, 0.0, TimeLine::Backward, 1.0},
                                  data{TimeLine::Backward, 1.0, TimeLine::Forward, 0.0});

        TimeLine timeLine(1000ms, test_data.initial_dir);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.setTargetRedirectMode(TimeLine::RedirectMode::Strict);
        timeLine.advance(0ms);

        REQUIRE(timeLine.direction() == test_data.initial_dir);
        REQUIRE(timeLine.value() == test_data.initial_val);
        QCOMPARE(timeLine.targetRedirectMode(), TimeLine::RedirectMode::Strict);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.advance(1000ms);
        REQUIRE(timeLine.value() == test_data.final_val);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());

        timeLine.setDirection(test_data.final_dir);

        REQUIRE(timeLine.direction() == test_data.final_dir);
        REQUIRE(timeLine.value() == test_data.final_val);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());
    }

    SECTION("relaxed redirect target mode")
    {
        struct data {
            TimeLine::Direction initial_dir;
            double initial_val;
            TimeLine::Direction final_dir;
            double final_val;
        };

        auto test_data = GENERATE(data{TimeLine::Forward, 0.0, TimeLine::Backward, 1.0},
                                  data{TimeLine::Backward, 1.0, TimeLine::Forward, 0.0});

        TimeLine timeLine(1000ms, test_data.initial_dir);
        timeLine.setEasingCurve(QEasingCurve::Linear);
        timeLine.setTargetRedirectMode(TimeLine::RedirectMode::Relaxed);
        timeLine.advance(0ms);

        REQUIRE(timeLine.direction() == test_data.initial_dir);
        REQUIRE(timeLine.value() == test_data.initial_val);
        QCOMPARE(timeLine.targetRedirectMode(), TimeLine::RedirectMode::Relaxed);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.advance(1000ms);
        REQUIRE(timeLine.value() == test_data.final_val);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());

        timeLine.setDirection(test_data.final_dir);
        timeLine.advance(1000ms);

        REQUIRE(timeLine.direction() == test_data.final_dir);
        REQUIRE(timeLine.value() == test_data.final_val);
        QVERIFY(!timeLine.running());
        QVERIFY(!timeLine.done());

        timeLine.advance(2000ms);
        REQUIRE(timeLine.direction() == test_data.final_dir);
        REQUIRE(timeLine.value() == test_data.initial_val);
        QVERIFY(!timeLine.running());
        QVERIFY(timeLine.done());
    }
}

}
