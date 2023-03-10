/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QtTest>
#include <catch2/catch_test_macros.hpp>

// In contrast to QTRY_LOOP_IMPL this uses QTestEventLoop isntead of qWait for making a step. This
// often works more reliable together with concurrent execution.
#define TRY_LOOP_IMPL(expr, timeoutValue, step)                                                    \
    if (!(expr)) {                                                                                 \
        QTest::qWait(0);                                                                           \
    }                                                                                              \
    int qt_test_i = 0;                                                                             \
    for (; qt_test_i < timeoutValue && !(expr); qt_test_i += step) {                               \
        QTestEventLoop loop;                                                                       \
        loop.enterLoopMSecs(step);                                                                 \
    }

#define TRY_IMPL(expr, timeout)                                                                    \
    const int qt_test_step = timeout < 350 ? timeout / 7 + 1 : 50;                                 \
    const int qt_test_timeoutValue = timeout;                                                      \
    {                                                                                              \
        TRY_LOOP_IMPL((expr), qt_test_timeoutValue, qt_test_step);                                 \
    }

// Will try to wait for the expression to become true while allowing event processing
#define TRY_REQUIRE_WITH_TIMEOUT(expr, timeout)                                                    \
    do {                                                                                           \
        TRY_IMPL((expr), timeout);                                                                 \
        REQUIRE(expr);                                                                             \
    } while (false)

#define TRY_REQUIRE(expr) TRY_REQUIRE_WITH_TIMEOUT(expr, 5000)

// Redefine Qt macros to the Catch ones. We may never use the Qt macros in their original form since
// tests will pass even if the macros fail.
#undef QCOMPARE
#undef QTRY_COMPARE
#undef QTRY_COMPARE_WITH_TIMEOUT
#undef QVERIFY
#undef QTRY_VERIFY
#undef QTRY_VERIFY_WITH_TIMEOUT

#define QCOMPARE(a, b) REQUIRE(a == b)
#define QTRY_COMPARE(a, b) TRY_REQUIRE(a == b)
#define QTRY_COMPARE_WITH_TIMEOUT(a, b, t) TRY_REQUIRE_WITH_TIMEOUT(a == b, t)
#define QVERIFY REQUIRE
#define QTRY_VERIFY TRY_REQUIRE
#define QTRY_VERIFY_WITH_TIMEOUT TRY_REQUIRE_WITH_TIMEOUT
