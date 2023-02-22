/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "render/compositor.h"
#include "render/scene.h"
#include "win/active_window.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/space.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <KGlobalAccel>
#include <KScreenLocker/KsldApp>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

namespace
{

void unlock()
{
    using namespace ScreenLocker;

    const auto children = KSldApp::self()->children();
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "LogindIntegration") != 0) {
            continue;
        }
        QMetaObject::invokeMethod(*it, "requestUnlock");
        return;
    }
    Q_ASSERT("Did not find 'requestUnlock' method in KSldApp. This should not happen!" == 0);
}

}

namespace KWin::detail::test
{

class HelperEffect : public Effect
{
    Q_OBJECT
public:
    HelperEffect()
    {
    }
    ~HelperEffect() override
    {
    }

    void windowInputMouseEvent(QEvent*) override
    {
        Q_EMIT inputEvent();
    }
    void grabbedKeyboardEvent(QKeyEvent* e) override
    {
        Q_EMIT keyEvent(e->text());
    }

Q_SIGNALS:
    void inputEvent();
    void keyEvent(const QString&);
};

#define LOCK                                                                                       \
    QVERIFY(!base::wayland::is_screen_locked(setup.base));                                         \
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),                                  \
                                   &ScreenLocker::KSldApp::lockStateChanged);                      \
    QVERIFY(lockStateChangedSpy.isValid());                                                        \
    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);                   \
    QCOMPARE(lockStateChangedSpy.count(), 1);                                                      \
    QVERIFY(base::wayland::is_screen_locked(setup.base));

// We use a while loop to check the spy condition repeatedly. We do not wait directly with a spy
// timer because this can be problematic with the screenlocker process acting simultaneously.
// Sporadically failing timers were observed on CI.
#define UNLOCK                                                                                     \
    int expectedLockCount = 1;                                                                     \
    if (ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::Locked) {             \
        expectedLockCount = 2;                                                                     \
    }                                                                                              \
    QCOMPARE(lockStateChangedSpy.count(), expectedLockCount);                                      \
    unlock();                                                                                      \
    while (lockStateChangedSpy.count() < expectedLockCount + 1) {                                  \
        QTest::qWait(100);                                                                         \
    }                                                                                              \
    QCOMPARE(lockStateChangedSpy.count(), expectedLockCount + 1);                                  \
    QVERIFY(!base::wayland::is_screen_locked(setup.base));

#define MOTION(target) Test::pointer_motion_absolute(target, timestamp++)

#define PRESS Test::pointer_button_pressed(BTN_LEFT, timestamp++)

#define RELEASE Test::pointer_button_released(BTN_LEFT, timestamp++)

#define KEYPRESS(key) Test::keyboard_key_pressed(key, timestamp++)

#define KEYRELEASE(key) Test::keyboard_key_released(key, timestamp++)

TEST_CASE("lockscreen", "[base]")
{
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    test::setup setup("lockscreen", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    Test::test_outputs_default();

    auto& scene = setup.base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);

    Test::setup_wayland_connection(Test::global_selection::seat);
    QVERIFY(Test::wait_for_wayland_pointer());

    Test::set_current_output(0);
    Test::cursor()->set_pos(QPoint(640, 512));

    std::unique_ptr<Wrapland::Client::Surface> surface_holder;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel_holder;

    auto showWindow = [&]() {
        using namespace Wrapland::Client;

        surface_holder = Test::create_surface();
        REQUIRE(surface_holder.get());
        toplevel_holder = Test::create_xdg_shell_toplevel(surface_holder);
        REQUIRE(toplevel_holder.get());

        // Let's render.
        auto c = Test::render_and_wait_for_shown(surface_holder, QSize(100, 50), Qt::blue);

        REQUIRE(c);
        REQUIRE(Test::get_wayland_window(setup.base->space->stacking.active) == c);

        return c;
    };

    SECTION("stacking order")
    {
        // This test verifies that the lockscreen greeter is placed above other windows.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        LOCK QVERIFY(clientAddedSpy.wait());

        auto window_id = clientAddedSpy.first().first().value<quint32>();
        auto client = Test::get_wayland_window(setup.base->space->windows_map.at(window_id));
        QVERIFY(client);
        QVERIFY(client->isLockScreen());
        QCOMPARE(win::get_layer(*client), win::layer::unmanaged);

        UNLOCK
    }

    SECTION("pointer")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        QVERIFY(pointer);

        QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
        QSignalSpy leftSpy(pointer.get(), &Pointer::left);
        QVERIFY(leftSpy.isValid());
        QVERIFY(enteredSpy.isValid());

        auto c = showWindow();
        QVERIFY(c);

        // First move cursor into the center of the window.
        quint32 timestamp = 1;
        MOTION(c->geo.frame.center());
        QVERIFY(enteredSpy.wait());

        LOCK

            QVERIFY(leftSpy.wait());
        QCOMPARE(leftSpy.count(), 1);

        // Simulate moving out in and out again.
        MOTION(c->geo.frame.center());
        MOTION(c->geo.frame.bottomRight() + QPoint(100, 100));
        MOTION(c->geo.frame.bottomRight() + QPoint(100, 100));
        QVERIFY(!leftSpy.wait(500));
        QCOMPARE(leftSpy.count(), 1);
        QCOMPARE(enteredSpy.count(), 1);

        // Go back on the window.
        MOTION(c->geo.frame.center());

        // And unlock.
        UNLOCK
        QTRY_COMPARE(enteredSpy.count(), 2);

        // Move on the window.
        MOTION(c->geo.frame.center() + QPoint(100, 100));
        QVERIFY(leftSpy.wait());
        MOTION(c->geo.frame.center());
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 3);
    }

    SECTION("pointer button")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        QVERIFY(pointer);

        QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
        QSignalSpy buttonChangedSpy(pointer.get(), &Pointer::buttonStateChanged);
        QVERIFY(enteredSpy.isValid());
        QVERIFY(buttonChangedSpy.isValid());

        auto c = showWindow();
        QVERIFY(c);

        // First move cursor into the center of the window.
        quint32 timestamp = 1;
        MOTION(c->geo.frame.center());
        QVERIFY(enteredSpy.wait());

        // And simulate a click.
        PRESS;
        QVERIFY(buttonChangedSpy.wait());
        RELEASE;
        QVERIFY(buttonChangedSpy.wait());

        LOCK

            // And simulate a click.
            PRESS;
        QVERIFY(!buttonChangedSpy.wait(500));
        RELEASE;
        QVERIFY(!buttonChangedSpy.wait(500));

        UNLOCK
        QTRY_COMPARE(enteredSpy.count(), 2);

        // And click again.
        PRESS;
        QVERIFY(buttonChangedSpy.wait());
        RELEASE;
        QVERIFY(buttonChangedSpy.wait());
    }

    SECTION("pointer axis")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        QVERIFY(pointer);

        QSignalSpy axisChangedSpy(pointer.get(), &Pointer::axisChanged);
        QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(axisChangedSpy.isValid());
        QVERIFY(enteredSpy.isValid());

        auto c = showWindow();
        QVERIFY(c);

        // First move cursor into the center of the window.
        quint32 timestamp = 1;
        MOTION(c->geo.frame.center());
        QVERIFY(enteredSpy.wait());

        // And simulate axis.
        Test::pointer_axis_horizontal(5.0, timestamp++, 0);
        QVERIFY(axisChangedSpy.wait());

        LOCK

            // Simulate axis one more time. Now without change.
            Test::pointer_axis_horizontal(5.0, timestamp++, 0);
        QVERIFY(!axisChangedSpy.wait(500));
        Test::pointer_axis_vertical(5.0, timestamp++, 0);
        QVERIFY(!axisChangedSpy.wait(500));

        // And unlock.
        UNLOCK
        QTRY_COMPARE(enteredSpy.count(), 2);

        // And move axis again.
        Test::pointer_axis_horizontal(5.0, timestamp++, 0);
        QVERIFY(axisChangedSpy.wait());
        Test::pointer_axis_vertical(5.0, timestamp++, 0);
        QVERIFY(axisChangedSpy.wait());
    }

    SECTION("keyboard")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
        QVERIFY(keyboard);

        QSignalSpy enteredSpy(keyboard.get(), &Keyboard::entered);
        QSignalSpy leftSpy(keyboard.get(), &Keyboard::left);
        QSignalSpy keyChangedSpy(keyboard.get(), &Keyboard::keyChanged);
        QVERIFY(enteredSpy.isValid());
        QVERIFY(leftSpy.isValid());
        QVERIFY(keyChangedSpy.isValid());

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(enteredSpy.wait());
        QTRY_COMPARE(enteredSpy.count(), 1);

        quint32 timestamp = 1;

        KEYPRESS(KEY_A);
        QVERIFY(keyChangedSpy.wait());
        QCOMPARE(keyChangedSpy.count(), 1);
        QCOMPARE(keyChangedSpy.at(0).at(0).value<quint32>(), quint32(KEY_A));
        QCOMPARE(keyChangedSpy.at(0).at(1).value<Keyboard::KeyState>(),
                 Keyboard::KeyState::Pressed);
        QCOMPARE(keyChangedSpy.at(0).at(2).value<quint32>(), quint32(1));

        KEYRELEASE(KEY_A);
        QVERIFY(keyChangedSpy.wait());
        QCOMPARE(keyChangedSpy.count(), 2);
        QCOMPARE(keyChangedSpy.at(1).at(0).value<quint32>(), quint32(KEY_A));
        QCOMPARE(keyChangedSpy.at(1).at(1).value<Keyboard::KeyState>(),
                 Keyboard::KeyState::Released);
        QCOMPARE(keyChangedSpy.at(1).at(2).value<quint32>(), quint32(2));

        LOCK QVERIFY(leftSpy.wait());

        KEYPRESS(KEY_B);
        KEYRELEASE(KEY_B);
        QCOMPARE(leftSpy.count(), 1);
        QCOMPARE(keyChangedSpy.count(), 2);

        UNLOCK
        QTRY_COMPARE(enteredSpy.count(), 2);

        KEYPRESS(KEY_C);
        QVERIFY(keyChangedSpy.wait());
        QCOMPARE(keyChangedSpy.count(), 3);

        KEYRELEASE(KEY_C);
        QVERIFY(keyChangedSpy.wait());
        QCOMPARE(keyChangedSpy.count(), 4);
        QCOMPARE(enteredSpy.count(), 2);
        QCOMPARE(keyChangedSpy.at(2).at(0).value<quint32>(), quint32(KEY_C));
        QCOMPARE(keyChangedSpy.at(3).at(0).value<quint32>(), quint32(KEY_C));
        QCOMPARE(keyChangedSpy.at(2).at(2).value<quint32>(), quint32(5));
        QCOMPARE(keyChangedSpy.at(3).at(2).value<quint32>(), quint32(6));
        QCOMPARE(keyChangedSpy.at(2).at(1).value<Keyboard::KeyState>(),
                 Keyboard::KeyState::Pressed);
        QCOMPARE(keyChangedSpy.at(3).at(1).value<Keyboard::KeyState>(),
                 Keyboard::KeyState::Released);
    }

    SECTION("screen edge")
    {
        QSignalSpy screenEdgeSpy(setup.base->space->edges->qobject.get(),
                                 &win::screen_edger_qobject::approaching);
        QVERIFY(screenEdgeSpy.isValid());
        QCOMPARE(screenEdgeSpy.count(), 0);

        quint32 timestamp = 1;
        MOTION(QPoint(5, 5));
        QCOMPARE(screenEdgeSpy.count(), 1);

        LOCK MOTION(QPoint(4, 4));
        QCOMPARE(screenEdgeSpy.count(), 1);

        UNLOCK
        MOTION(QPoint(5, 5));
        QCOMPARE(screenEdgeSpy.count(), 2);
    }

    SECTION("effects")
    {
        std::unique_ptr<HelperEffect> effect(new HelperEffect);
        QSignalSpy inputSpy(effect.get(), &HelperEffect::inputEvent);
        QVERIFY(inputSpy.isValid());

        effects->startMouseInterception(effect.get(), Qt::ArrowCursor);

        quint32 timestamp = 1;
        QCOMPARE(inputSpy.count(), 0);
        MOTION(QPoint(5, 5));
        QCOMPARE(inputSpy.count(), 1);

        // Simlate click.
        PRESS;
        QCOMPARE(inputSpy.count(), 2);
        RELEASE;
        QCOMPARE(inputSpy.count(), 3);

        LOCK MOTION(QPoint(6, 6));
        QCOMPARE(inputSpy.count(), 3);

        // Simlate click.
        PRESS;
        QCOMPARE(inputSpy.count(), 3);
        RELEASE;
        QCOMPARE(inputSpy.count(), 3);

        UNLOCK
        MOTION(QPoint(5, 5));
        QCOMPARE(inputSpy.count(), 4);

        // Simlate click.
        PRESS;
        QCOMPARE(inputSpy.count(), 5);
        RELEASE;
        QCOMPARE(inputSpy.count(), 6);

        effects->stopMouseInterception(effect.get());
    }

    SECTION("effects keyboard")
    {
        std::unique_ptr<HelperEffect> effect(new HelperEffect);
        QSignalSpy inputSpy(effect.get(), &HelperEffect::keyEvent);
        QVERIFY(inputSpy.isValid());
        effects->grabKeyboard(effect.get());

        quint32 timestamp = 1;

        KEYPRESS(KEY_A);
        QCOMPARE(inputSpy.count(), 1);
        QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));

        KEYRELEASE(KEY_A);
        QCOMPARE(inputSpy.count(), 2);
        QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
        QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));

        LOCK KEYPRESS(KEY_B);
        QCOMPARE(inputSpy.count(), 2);

        KEYRELEASE(KEY_B);
        QCOMPARE(inputSpy.count(), 2);

        UNLOCK
        KEYPRESS(KEY_C);
        QCOMPARE(inputSpy.count(), 3);
        QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
        QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));
        QCOMPARE(inputSpy.at(2).first().toString(), QStringLiteral("c"));

        KEYRELEASE(KEY_C);
        QCOMPARE(inputSpy.count(), 4);
        QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
        QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));
        QCOMPARE(inputSpy.at(2).first().toString(), QStringLiteral("c"));
        QCOMPARE(inputSpy.at(3).first().toString(), QStringLiteral("c"));

        effects->ungrabKeyboard();
    }

    SECTION("effects keyboard autorepeat")
    {
        // This test is just like testEffectsKeyboard, but tests auto repeat key events
        // while the key is pressed the Effect should get auto repeated events
        // but the lock screen should filter them out.

        std::unique_ptr<HelperEffect> effect(new HelperEffect);
        QSignalSpy inputSpy(effect.get(), &HelperEffect::keyEvent);
        QVERIFY(inputSpy.isValid());

        effects->grabKeyboard(effect.get());

        // We need to configure the key repeat first. It is only enabled on libinput.
        setup.base->server->seat()->keyboards().set_repeat_info(25, 300);

        quint32 timestamp = 1;

        KEYPRESS(KEY_A);
        QCOMPARE(inputSpy.count(), 1);
        QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
        QVERIFY(inputSpy.wait());
        QVERIFY(inputSpy.count() > 1);

        // And still more events.
        QVERIFY(inputSpy.wait());
        QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));

        // Now release.
        inputSpy.clear();
        KEYRELEASE(KEY_A);
        QCOMPARE(inputSpy.count(), 1);

        // While locked key repeat should not pass any events to the Effect.
        LOCK KEYPRESS(KEY_B);
        QVERIFY(!inputSpy.wait(500));
        KEYRELEASE(KEY_B);
        QVERIFY(!inputSpy.wait(500));

        // Don't test again, that's covered by testEffectsKeyboard.
        UNLOCK

        effects->ungrabKeyboard();
    }

    SECTION("move window")
    {
        using namespace Wrapland::Client;

        auto c = showWindow();
        QVERIFY(c);

        QSignalSpy clientStepUserMovedResizedSpy(c->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        quint32 timestamp = 1;

        win::active_window_move(*setup.base->space);
        QCOMPARE(Test::get_wayland_window(setup.base->space->move_resize_window), c);
        QVERIFY(win::is_move(c));

        Test::keyboard_key_pressed(KEY_RIGHT, timestamp++);
        Test::keyboard_key_released(KEY_RIGHT, timestamp++);
        QEXPECT_FAIL("", "First event is ignored", Continue);
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        // TODO: Adjust once the expected fail is fixed.
        Test::keyboard_key_pressed(KEY_RIGHT, timestamp++);
        Test::keyboard_key_released(KEY_RIGHT, timestamp++);
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        // While locking our window should continue to be in move resize.
        LOCK;
        QCOMPARE(Test::get_wayland_window(setup.base->space->move_resize_window), c);
        QVERIFY(win::is_move(c));
        Test::keyboard_key_pressed(KEY_RIGHT, timestamp++);
        Test::keyboard_key_released(KEY_RIGHT, timestamp++);
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        UNLOCK
        QCOMPARE(Test::get_wayland_window(setup.base->space->move_resize_window), c);
        QVERIFY(win::is_move(c));

        Test::keyboard_key_pressed(KEY_RIGHT, timestamp++);
        Test::keyboard_key_released(KEY_RIGHT, timestamp++);
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);

        Test::keyboard_key_pressed(KEY_ESC, timestamp++);
        Test::keyboard_key_released(KEY_ESC, timestamp++);
        QVERIFY(!win::is_move(c));
    }

    SECTION("pointer shortcut")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<QAction> action(new QAction(nullptr));
        QSignalSpy actionSpy(action.get(), &QAction::triggered);
        QVERIFY(actionSpy.isValid());

        input::platform_register_pointer_shortcut(
            *setup.base->input, Qt::MetaModifier, Qt::LeftButton, action.get());

        // Try to trigger the shortcut.
        quint32 timestamp = 1;

#define PERFORM(expectedCount)                                                                     \
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);                                         \
    PRESS;                                                                                         \
    QCoreApplication::instance()->processEvents();                                                 \
    QCOMPARE(actionSpy.count(), expectedCount);                                                    \
    RELEASE;                                                                                       \
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);                                        \
    QCoreApplication::instance()->processEvents();                                                 \
    QCOMPARE(actionSpy.count(), expectedCount);

        PERFORM(1)

        // Now the same thing with a locked screen.
        LOCK PERFORM(1)

            // And as unlocked.
            UNLOCK PERFORM(2)
#undef PERFORM
    }

    SECTION("axis shortcut")
    {
        using namespace Wrapland::Client;

        auto direction = GENERATE(Qt::Vertical, Qt::Horizontal);
        auto sign = GENERATE(-1, 1);

        std::unique_ptr<QAction> action(new QAction(nullptr));
        QSignalSpy actionSpy(action.get(), &QAction::triggered);
        QVERIFY(actionSpy.isValid());

        PointerAxisDirection axisDirection = PointerAxisUp;

        if (direction == Qt::Vertical) {
            axisDirection = sign > 0 ? PointerAxisUp : PointerAxisDown;
        } else {
            axisDirection = sign > 0 ? PointerAxisLeft : PointerAxisRight;
        }

        input::platform_register_axis_shortcut(
            *setup.base->input, Qt::MetaModifier, axisDirection, action.get());

        // Try to trigger the shortcut.
        quint32 timestamp = 1;

#define PERFORM(expectedCount)                                                                     \
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);                                         \
    if (direction == Qt::Vertical)                                                                 \
        Test::pointer_axis_vertical(sign * 5.0, timestamp++, 0);                                   \
    else                                                                                           \
        Test::pointer_axis_horizontal(sign * 5.0, timestamp++, 0);                                 \
    QCoreApplication::instance()->processEvents();                                                 \
    QCOMPARE(actionSpy.count(), expectedCount);                                                    \
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);                                        \
    QCoreApplication::instance()->processEvents();                                                 \
    QCOMPARE(actionSpy.count(), expectedCount);

        PERFORM(1)

        // Now the same thing with a locked screen.
        LOCK PERFORM(1)

            // And as unlocked.
            UNLOCK PERFORM(2)

#undef PERFORM
    }

    SECTION("keyboard shortcut")
    {
        using namespace Wrapland::Client;

        std::unique_ptr<QAction> action(new QAction(nullptr));
        QSignalSpy actionSpy(action.get(), &QAction::triggered);
        QVERIFY(actionSpy.isValid());

        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName("LockScreenTest::testKeyboardShortcut");

        KGlobalAccel::self()->setDefaultShortcut(
            action.get(), QList<QKeySequence>{Qt::CTRL + Qt::META + Qt::ALT + Qt::Key_Space});
        KGlobalAccel::self()->setShortcut(
            action.get(),
            QList<QKeySequence>{Qt::CTRL + Qt::META + Qt::ALT + Qt::Key_Space},
            KGlobalAccel::NoAutoloading);

        // Try to trigger the shortcut.
        quint32 timestamp = 1;

        KEYPRESS(KEY_LEFTCTRL);
        KEYPRESS(KEY_LEFTMETA);
        KEYPRESS(KEY_LEFTALT);
        KEYPRESS(KEY_SPACE);

        QVERIFY(actionSpy.wait());
        QCOMPARE(actionSpy.count(), 1);

        KEYRELEASE(KEY_SPACE);
        QVERIFY(!actionSpy.wait(500));
        QCOMPARE(actionSpy.count(), 1);

        LOCK KEYPRESS(KEY_SPACE);
        QVERIFY(!actionSpy.wait(500));
        QCOMPARE(actionSpy.count(), 1);

        KEYRELEASE(KEY_SPACE);
        QVERIFY(!actionSpy.wait(500));
        QCOMPARE(actionSpy.count(), 1);

        UNLOCK
        KEYPRESS(KEY_SPACE);
        QVERIFY(actionSpy.wait());
        QCOMPARE(actionSpy.count(), 2);

        KEYRELEASE(KEY_SPACE);
        QVERIFY(!actionSpy.wait(500));
        QCOMPARE(actionSpy.count(), 2);

        KEYRELEASE(KEY_LEFTCTRL);
        KEYRELEASE(KEY_LEFTMETA);
        KEYRELEASE(KEY_LEFTALT);
    }

    SECTION("touch")
    {
        using namespace Wrapland::Client;

        auto touch = Test::get_client().interfaces.seat->createTouch(
            Test::get_client().interfaces.seat.get());
        QVERIFY(touch);
        QVERIFY(touch->isValid());

        auto c = showWindow();
        QVERIFY(c);

        QSignalSpy sequenceStartedSpy(touch, &Touch::sequenceStarted);
        QVERIFY(sequenceStartedSpy.isValid());
        QSignalSpy cancelSpy(touch, &Touch::sequenceCanceled);
        QVERIFY(cancelSpy.isValid());
        QSignalSpy pointRemovedSpy(touch, &Touch::pointRemoved);
        QVERIFY(pointRemovedSpy.isValid());

        quint32 timestamp = 1;

        Test::touch_down(1, QPointF(25, 25), timestamp++);
        QVERIFY(sequenceStartedSpy.wait());
        QCOMPARE(sequenceStartedSpy.count(), 1);

        LOCK QVERIFY(cancelSpy.wait());

        Test::touch_up(1, timestamp++);

        QVERIFY(!pointRemovedSpy.wait(500));
        Test::touch_down(1, QPointF(25, 25), timestamp++);
        Test::touch_motion(1, QPointF(26, 26), timestamp++);
        Test::touch_up(1, timestamp++);

        UNLOCK
        Test::touch_down(1, QPointF(25, 25), timestamp++);
        QVERIFY(sequenceStartedSpy.wait());
        QCOMPARE(sequenceStartedSpy.count(), 2);

        Test::touch_up(1, timestamp++);
        QVERIFY(pointRemovedSpy.wait());
        QCOMPARE(pointRemovedSpy.count(), 1);
    }
}

}

#include "lockscreen.moc"
