/*
SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/catch_macros.h"

#include "win/input/gestures.h"

#include <QSignalSpy>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("gestures unit", "[input],[unit]")
{
    SECTION("swipe min finger")
    {
        auto count = GENERATE(0, 1, 10);

        win::swipe_gesture swipeGesture;
        QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), false);
        QCOMPARE(swipeGesture.minimumFingerCount(), 0u);

        swipeGesture.setMinimumFingerCount(count);
        QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), true);
        REQUIRE(swipeGesture.minimumFingerCount() == count);

        swipeGesture.setMinimumFingerCount(0);
        QCOMPARE(swipeGesture.minimumFingerCountIsRelevant(), true);
        QCOMPARE(swipeGesture.minimumFingerCount(), 0u);
    }

    SECTION("pinch min finger")
    {
        auto count = GENERATE(0, 1, 10);

        win::pinch_gesture pinchGesture;
        QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), false);
        QCOMPARE(pinchGesture.minimumFingerCount(), 0u);

        pinchGesture.setMinimumFingerCount(count);
        QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), true);
        REQUIRE(pinchGesture.minimumFingerCount() == count);

        pinchGesture.setMinimumFingerCount(0);
        QCOMPARE(pinchGesture.minimumFingerCountIsRelevant(), true);
        QCOMPARE(pinchGesture.minimumFingerCount(), 0u);
    }

    SECTION("swipe max finger")
    {
        auto count = GENERATE(0, 1, 10);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), false);
        QCOMPARE(gesture.maximumFingerCount(), 0u);

        gesture.setMaximumFingerCount(count);
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
        REQUIRE(gesture.maximumFingerCount() == count);

        gesture.setMaximumFingerCount(0);
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
        QCOMPARE(gesture.maximumFingerCount(), 0u);
    }

    SECTION("pinch max finger")
    {
        auto count = GENERATE(0, 1, 10);

        win::pinch_gesture gesture;
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), false);
        QCOMPARE(gesture.maximumFingerCount(), 0u);

        gesture.setMaximumFingerCount(count);
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
        REQUIRE(gesture.maximumFingerCount() == count);

        gesture.setMaximumFingerCount(0);
        QCOMPARE(gesture.maximumFingerCountIsRelevant(), true);
        QCOMPARE(gesture.maximumFingerCount(), 0u);
    }

    SECTION("swipe direction")
    {
        auto swipe_direction = GENERATE(
            SwipeDirection::Up, SwipeDirection::Left, SwipeDirection::Right, SwipeDirection::Down);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.direction(), SwipeDirection::Down);

        gesture.setDirection(swipe_direction);
        QCOMPARE(gesture.direction(), swipe_direction);

        gesture.setDirection(SwipeDirection::Down);
        QCOMPARE(gesture.direction(), SwipeDirection::Down);
    }

    SECTION("pinch direction")
    {
        auto pinch_direction = GENERATE(PinchDirection::Contracting, PinchDirection::Expanding);

        win::pinch_gesture gesture;
        QCOMPARE(gesture.direction(), PinchDirection::Expanding);

        gesture.setDirection(pinch_direction);
        QCOMPARE(gesture.direction(), pinch_direction);

        gesture.setDirection(PinchDirection::Expanding);
        QCOMPARE(gesture.direction(), PinchDirection::Expanding);
    }

    SECTION("minimum x")
    {
        auto min = GENERATE(0, -1, 1);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.minimumX(), 0);
        QCOMPARE(gesture.minimumXIsRelevant(), false);

        gesture.setMinimumX(min);
        QCOMPARE(gesture.minimumX(), min);
        QCOMPARE(gesture.minimumXIsRelevant(), true);
    }

    SECTION("minimum y")
    {
        auto min = GENERATE(0, -1, 1);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.minimumY(), 0);
        QCOMPARE(gesture.minimumYIsRelevant(), false);

        gesture.setMinimumY(min);
        QCOMPARE(gesture.minimumY(), min);
        QCOMPARE(gesture.minimumYIsRelevant(), true);
    }

    SECTION("maximum x")
    {
        auto max = GENERATE(0, -1, 1);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.maximumX(), 0);
        QCOMPARE(gesture.maximumXIsRelevant(), false);

        gesture.setMaximumX(max);
        QCOMPARE(gesture.maximumX(), max);
        QCOMPARE(gesture.maximumXIsRelevant(), true);
    }

    SECTION("maximum y")
    {
        auto max = GENERATE(0, -1, 1);

        win::swipe_gesture gesture;
        QCOMPARE(gesture.maximumY(), 0);
        QCOMPARE(gesture.maximumYIsRelevant(), false);

        gesture.setMaximumY(max);
        QCOMPARE(gesture.maximumY(), max);
        QCOMPARE(gesture.maximumYIsRelevant(), true);
    }

    SECTION("start geometry")
    {
        win::swipe_gesture gesture;
        gesture.setStartGeometry(QRect(1, 2, 20, 30));
        QCOMPARE(gesture.minimumXIsRelevant(), true);
        QCOMPARE(gesture.minimumYIsRelevant(), true);
        QCOMPARE(gesture.maximumXIsRelevant(), true);
        QCOMPARE(gesture.maximumYIsRelevant(), true);
        QCOMPARE(gesture.minimumX(), 1);
        QCOMPARE(gesture.minimumY(), 2);
        QCOMPARE(gesture.maximumX(), 21);
        QCOMPARE(gesture.maximumY(), 32);
    }

    SECTION("set minimum delta")
    {
        win::swipe_gesture swipeGesture;
        QCOMPARE(swipeGesture.isMinimumDeltaRelevant(), false);
        QCOMPARE(swipeGesture.minimumDelta(), QSizeF());
        QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF()), true);
        swipeGesture.setMinimumDelta(QSizeF(2, 3));
        QCOMPARE(swipeGesture.isMinimumDeltaRelevant(), true);
        QCOMPARE(swipeGesture.minimumDelta(), QSizeF(2, 3));
        QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF()), false);
        QCOMPARE(swipeGesture.minimumDeltaReached(QSizeF(2, 3)), true);

        win::pinch_gesture pinchGesture;
        QCOMPARE(pinchGesture.isMinimumScaleDeltaRelevant(), false);
        QCOMPARE(pinchGesture.minimumScaleDelta(), win::DEFAULT_MINIMUM_SCALE_DELTA);
        QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.25), true);
        pinchGesture.setMinimumScaleDelta(.5);
        QCOMPARE(pinchGesture.isMinimumScaleDeltaRelevant(), true);
        QCOMPARE(pinchGesture.minimumScaleDelta(), .5);
        QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.24), false);
        QCOMPARE(pinchGesture.minimumScaleDeltaReached(1.5), true);
    }

    SECTION("minimum delta reached")
    {
        struct data {
            SwipeDirection direction;
            QSizeF min_delta;
            QSizeF delta;
            bool reached;
            double progress;
        };

        auto test_data
            = GENERATE(data{SwipeDirection::Up, {0, -30}, {0, -40}, true, 1.0},
                       data{SwipeDirection::Up, {0, -30}, {0, -30}, true, 1.0},
                       data{SwipeDirection::Up, {0, -30}, {0, -29}, false, 29.0 / 30.0},
                       data{SwipeDirection::Left, {30, -30}, {-40, 20}, true, 1.0},
                       data{SwipeDirection::Left, {30, -40}, {-30, 0}, true, 1.0},
                       data{SwipeDirection::Left, {30, -30}, {-29, 0}, false, 29.0 / 30.0},
                       data{SwipeDirection::Right, {30, -30}, {40, 20}, true, 1.0},
                       data{SwipeDirection::Right, {30, -40}, {30, 0}, true, 1.0},
                       data{SwipeDirection::Right, {30, -30}, {29, 0}, false, 29.0 / 30.0},
                       data{SwipeDirection::Down, {0, 30}, {0, 40}, true, 1.0},
                       data{SwipeDirection::Down, {0, 30}, {0, 30}, true, 1.0},
                       data{SwipeDirection::Down, {0, 30}, {0, 29}, false, 29.0 / 30.0});

        win::gesture_recognizer recognizer;

        // swipe gesture
        win::swipe_gesture gesture;
        ;
        gesture.setDirection(test_data.direction);
        gesture.setMinimumDelta(test_data.min_delta);
        QCOMPARE(gesture.minimumDeltaReached(test_data.delta), test_data.reached);

        recognizer.registerSwipeGesture(&gesture);

        QSignalSpy startedSpy(&gesture, &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy triggeredSpy(&gesture, &win::swipe_gesture::triggered);
        QVERIFY(triggeredSpy.isValid());
        QSignalSpy cancelledSpy(&gesture, &win::swipe_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());
        QSignalSpy progressSpy(&gesture, &win::swipe_gesture::progress);
        QVERIFY(progressSpy.isValid());

        recognizer.startSwipeGesture(1);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(triggeredSpy.count(), 0);
        QCOMPARE(cancelledSpy.count(), 0);
        QCOMPARE(progressSpy.count(), 0);

        recognizer.updateSwipeGesture(test_data.delta);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(triggeredSpy.count(), 0);
        QCOMPARE(cancelledSpy.count(), 0);
        QCOMPARE(progressSpy.count(), 1);
        REQUIRE(progressSpy.first().first().value<qreal>() == test_data.progress);

        recognizer.endSwipeGesture();
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(progressSpy.count(), 1);
        QCOMPARE(triggeredSpy.isEmpty(), !test_data.reached);
        QCOMPARE(cancelledSpy.isEmpty(), test_data.reached);
    }

    SECTION("minimum scale delta")
    {
        // pinch gesture
        win::pinch_gesture gesture;
        gesture.setDirection(PinchDirection::Contracting);
        gesture.setMinimumScaleDelta(.5);
        gesture.setMinimumFingerCount(3);
        gesture.setMaximumFingerCount(4);

        QCOMPARE(gesture.minimumScaleDeltaReached(1.25), false);
        QCOMPARE(gesture.minimumScaleDeltaReached(1.5), true);

        win::gesture_recognizer recognizer;
        recognizer.registerPinchGesture(&gesture);

        QSignalSpy startedSpy(&gesture, &win::pinch_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy triggeredSpy(&gesture, &win::pinch_gesture::triggered);
        QVERIFY(triggeredSpy.isValid());
        QSignalSpy cancelledSpy(&gesture, &win::pinch_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());
        QSignalSpy progressSpy(&gesture, &win::pinch_gesture::progress);
        QVERIFY(progressSpy.isValid());

        recognizer.startPinchGesture(4);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(triggeredSpy.count(), 0);
        QCOMPARE(cancelledSpy.count(), 0);
        QCOMPARE(progressSpy.count(), 0);
    }

    SECTION("unregister swipe cancels")
    {
        win::gesture_recognizer recognizer;
        QScopedPointer<win::swipe_gesture> gesture(new win::swipe_gesture);
        QSignalSpy startedSpy(gesture.data(), &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy cancelledSpy(gesture.data(), &win::swipe_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());

        recognizer.registerSwipeGesture(gesture.data());
        recognizer.startSwipeGesture(1);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(cancelledSpy.count(), 0);
        recognizer.unregisterSwipeGesture(gesture.data());
        QCOMPARE(cancelledSpy.count(), 1);

        // delete the gesture should not trigger cancel
        gesture.reset();
        QCOMPARE(cancelledSpy.count(), 1);
    }

    SECTION("unregister pinch cancels")
    {
        win::gesture_recognizer recognizer;
        QScopedPointer<win::pinch_gesture> gesture(new win::pinch_gesture);
        QSignalSpy startedSpy(gesture.data(), &win::pinch_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy cancelledSpy(gesture.data(), &win::pinch_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());

        recognizer.registerPinchGesture(gesture.data());
        recognizer.startPinchGesture(1);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(cancelledSpy.count(), 0);
        recognizer.unregisterPinchGesture(gesture.data());
        QCOMPARE(cancelledSpy.count(), 1);

        // delete the gesture should not trigger cancel
        gesture.reset();
        QCOMPARE(cancelledSpy.count(), 1);
    }

    SECTION("delete swipe cancels")
    {
        win::gesture_recognizer recognizer;
        QScopedPointer<win::swipe_gesture> gesture(new win::swipe_gesture);
        QSignalSpy startedSpy(gesture.data(), &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy cancelledSpy(gesture.data(), &win::swipe_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());

        recognizer.registerSwipeGesture(gesture.data());
        recognizer.startSwipeGesture(1);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(cancelledSpy.count(), 0);
        gesture.reset();
        QCOMPARE(cancelledSpy.count(), 1);
    }

    SECTION("swipe cancel")
    {
        auto direction = GENERATE(
            SwipeDirection::Up, SwipeDirection::Left, SwipeDirection::Right, SwipeDirection::Down);

        win::gesture_recognizer recognizer;
        QScopedPointer<win::swipe_gesture> gesture(new win::swipe_gesture);

        gesture->setDirection(direction);
        QSignalSpy startedSpy(gesture.data(), &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());
        QSignalSpy cancelledSpy(gesture.data(), &win::swipe_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());
        QSignalSpy triggeredSpy(gesture.data(), &win::swipe_gesture::triggered);
        QVERIFY(triggeredSpy.isValid());

        recognizer.registerSwipeGesture(gesture.data());
        recognizer.startSwipeGesture(1);
        QCOMPARE(startedSpy.count(), 1);
        QCOMPARE(cancelledSpy.count(), 0);
        recognizer.cancelSwipeGesture();
        QCOMPARE(cancelledSpy.count(), 1);
        QCOMPARE(triggeredSpy.count(), 0);
    }

    SECTION("swipe update trigger")
    {
        struct data {
            SwipeDirection direction;
            QSizeF delta;
        };

        auto test_data = GENERATE(data{SwipeDirection::Up, {2, -3}},
                                  data{SwipeDirection::Left, {-3, 1}},
                                  data{SwipeDirection::Right, {20, -19}},
                                  data{SwipeDirection::Down, {0, 50}});

        win::gesture_recognizer recognizer;
        win::swipe_gesture gesture;

        gesture.setDirection(test_data.direction);

        QSignalSpy triggeredSpy(&gesture, &win::swipe_gesture::triggered);
        QVERIFY(triggeredSpy.isValid());
        QSignalSpy cancelledSpy(&gesture, &win::swipe_gesture::cancelled);
        QVERIFY(cancelledSpy.isValid());

        recognizer.registerSwipeGesture(&gesture);

        recognizer.startSwipeGesture(1);
        recognizer.updateSwipeGesture(test_data.delta);
        QCOMPARE(cancelledSpy.count(), 0);
        QCOMPARE(triggeredSpy.count(), 0);

        recognizer.endSwipeGesture();
        QCOMPARE(cancelledSpy.count(), 0);
        QCOMPARE(triggeredSpy.count(), 1);
    }

    SECTION("swipe min finger start")
    {
        struct data {
            unsigned int min;
            unsigned int count;
            bool started;
        };

        auto test_data = GENERATE(data{1, 1, true}, data{2, 1, false}, data{1, 2, true});

        win::gesture_recognizer recognizer;
        win::swipe_gesture gesture;
        gesture.setMinimumFingerCount(test_data.min);

        QSignalSpy startedSpy(&gesture, &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());

        recognizer.registerSwipeGesture(&gesture);
        recognizer.startSwipeGesture(test_data.count);
        REQUIRE(startedSpy.isEmpty() != test_data.started);
    }

    SECTION("swipe max finger start")
    {
        struct data {
            unsigned int max;
            unsigned int count;
            bool started;
        };

        auto test_data = GENERATE(data{1, 1, true}, data{2, 1, true}, data{1, 2, false});

        win::gesture_recognizer recognizer;
        win::swipe_gesture gesture;
        gesture.setMaximumFingerCount(test_data.max);

        QSignalSpy startedSpy(&gesture, &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());

        recognizer.registerSwipeGesture(&gesture);
        recognizer.startSwipeGesture(test_data.count);
        REQUIRE(startedSpy.isEmpty() != test_data.started);
    }

    SECTION("not emit callbacks before direction decided")
    {
        win::gesture_recognizer recognizer;
        win::swipe_gesture up;
        win::swipe_gesture down;
        win::swipe_gesture right;
        win::pinch_gesture expand;
        win::pinch_gesture contract;
        up.setDirection(SwipeDirection::Up);
        down.setDirection(SwipeDirection::Down);
        right.setDirection(SwipeDirection::Right);
        expand.setDirection(PinchDirection::Expanding);
        contract.setDirection(PinchDirection::Contracting);
        recognizer.registerSwipeGesture(&up);
        recognizer.registerSwipeGesture(&down);
        recognizer.registerSwipeGesture(&right);
        recognizer.registerPinchGesture(&expand);
        recognizer.registerPinchGesture(&contract);

        QSignalSpy upSpy(&up, &win::swipe_gesture::progress);
        QSignalSpy downSpy(&down, &win::swipe_gesture::progress);
        QSignalSpy rightSpy(&right, &win::swipe_gesture::progress);
        QSignalSpy expandSpy(&expand, &win::pinch_gesture::progress);
        QSignalSpy contractSpy(&contract, &win::pinch_gesture::progress);

        // don't release callback until we know the direction of swipe gesture
        recognizer.startSwipeGesture(4);
        QCOMPARE(upSpy.count(), 0);
        QCOMPARE(downSpy.count(), 0);
        QCOMPARE(rightSpy.count(), 0);

        // up (negative y)
        recognizer.updateSwipeGesture(QSizeF(0, -1.5));
        QCOMPARE(upSpy.count(), 1);
        QCOMPARE(downSpy.count(), 0);
        QCOMPARE(rightSpy.count(), 0);

        // down (positive y)
        // recognizer.updateSwipeGesture(QSizeF(0, 0));
        recognizer.updateSwipeGesture(QSizeF(0, 3));
        QCOMPARE(upSpy.count(), 1);
        QCOMPARE(downSpy.count(), 1);
        QCOMPARE(rightSpy.count(), 0);

        // right
        recognizer.cancelSwipeGesture();
        recognizer.startSwipeGesture(4);
        recognizer.updateSwipeGesture(QSizeF(1, 0));
        QCOMPARE(upSpy.count(), 1);
        QCOMPARE(downSpy.count(), 1);
        QCOMPARE(rightSpy.count(), 1);

        recognizer.cancelSwipeGesture();

        // same test for pinch gestures
        recognizer.startPinchGesture(4);
        QCOMPARE(expandSpy.count(), 0);
        QCOMPARE(contractSpy.count(), 0);

        // contracting
        recognizer.updatePinchGesture(.5, 0, QSizeF(0, 0));
        QCOMPARE(expandSpy.count(), 0);
        QCOMPARE(contractSpy.count(), 1);

        // expanding
        recognizer.updatePinchGesture(1.5, 0, QSizeF(0, 0));
        QCOMPARE(expandSpy.count(), 1);
        QCOMPARE(contractSpy.count(), 1);
    }

    SECTION("swipe geometry start")
    {
        struct data {
            QRect geometry;
            QPointF start_pos;
            bool started;
        };

        // top left/right, bottom left/right, x/y too small, x/y too large, inside
        auto test_data = GENERATE(data{{0, 0, 10, 20}, {0, 0}, true},
                                  data{{0, 0, 10, 20}, {10, 0}, true},
                                  data{{0, 0, 10, 20}, {0, 20}, true},
                                  data{{0, 0, 10, 20}, {10, 20}, true},
                                  data{{10, 20, 30, 40}, {9, 25}, false},
                                  data{{10, 20, 30, 40}, {25, 19}, false},
                                  data{{10, 20, 30, 40}, {41, 25}, false},
                                  data{{10, 20, 30, 40}, {25, 61}, false},
                                  data{{10, 20, 30, 40}, {25, 25}, true});

        win::gesture_recognizer recognizer;
        win::swipe_gesture gesture;
        gesture.setStartGeometry(test_data.geometry);

        QSignalSpy startedSpy(&gesture, &win::swipe_gesture::started);
        QVERIFY(startedSpy.isValid());

        recognizer.registerSwipeGesture(&gesture);
        recognizer.startSwipeGesture(test_data.start_pos);
        REQUIRE(startedSpy.isEmpty() != test_data.started);
    }
}

}
