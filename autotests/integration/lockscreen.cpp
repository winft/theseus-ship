/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright © 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright © 2020 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "kwin_wayland_test.h"

#include "composite.h"
#include "cursor.h"
#include "platform.h"
#include "scene.h"
#include "screenedge.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/move.h"

#include <kwineffects.h>

#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

#include <Wrapland/Server/seat.h>

#include <KGlobalAccel>
#include <KScreenLocker/KsldApp>

#include <linux/input.h>

Q_DECLARE_METATYPE(Qt::Orientation)

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_lock_screen-0");

class LockScreenTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testStackingOrder();
    void testPointer();
    void testPointerButton();
    void testPointerAxis();
    void testKeyboard();
    void testScreenEdge();
    void testEffects();
    void testEffectsKeyboard();
    void testEffectsKeyboardAutorepeat();
    void testMoveWindow();
    void testPointerShortcut();
    void testAxisShortcut_data();
    void testAxisShortcut();
    void testKeyboardShortcut();
    void testTouch();

private:
    void unlock();
    Toplevel* showWindow();

    Wrapland::Client::ConnectionThread *m_connection = nullptr;
    Wrapland::Client::Compositor *m_compositor = nullptr;
    Wrapland::Client::Seat *m_seat = nullptr;
    Wrapland::Client::ShmPool *m_shm = nullptr;
    Wrapland::Client::Shell *m_shell = nullptr;

    std::unique_ptr<Wrapland::Client::Surface> surface_holder;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel_holder;
};

class HelperEffect : public Effect
{
    Q_OBJECT
public:
    HelperEffect() {}
    ~HelperEffect() override {}

    void windowInputMouseEvent(QEvent*) override {
        emit inputEvent();
    }
    void grabbedKeyboardEvent(QKeyEvent *e) override {
        emit keyEvent(e->text());
    }

Q_SIGNALS:
    void inputEvent();
    void keyEvent(const QString&);
};

#define LOCK \
    QVERIFY(!waylandServer()->isScreenLocked()); \
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(), \
                                   &ScreenLocker::KSldApp::lockStateChanged); \
    QVERIFY(lockStateChangedSpy.isValid()); \
    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate); \
    QCOMPARE(lockStateChangedSpy.count(), 1); \
    QVERIFY(waylandServer()->isScreenLocked());

// We use a while loop to check the spy condition repeatedly. We do not wait directly with a spy
// timer because this can be problematic with the screenlocker process acting simultaneously.
// Sporadically failing timers were observed on CI.
#define UNLOCK \
    int expectedLockCount = 1; \
    if (ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::Locked) { \
        expectedLockCount = 2; \
    } \
    QCOMPARE(lockStateChangedSpy.count(), expectedLockCount); \
    unlock(); \
    while (lockStateChangedSpy.count() < expectedLockCount + 1) { \
        QTest::qWait(100); \
    } \
    QCOMPARE(lockStateChangedSpy.count(), expectedLockCount + 1); \
    QVERIFY(!waylandServer()->isScreenLocked());


#define MOTION(target) \
    kwinApp()->platform()->pointerMotion(target, timestamp++)

#define PRESS \
    kwinApp()->platform()->pointerButtonPressed(BTN_LEFT, timestamp++)

#define RELEASE \
    kwinApp()->platform()->pointerButtonReleased(BTN_LEFT, timestamp++)

#define KEYPRESS( key ) \
    kwinApp()->platform()->keyboardKeyPressed(key, timestamp++)

#define KEYRELEASE( key ) \
    kwinApp()->platform()->keyboardKeyReleased(key, timestamp++)

void LockScreenTest::unlock()
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

Toplevel* LockScreenTest::showWindow()
{
    using namespace Wrapland::Client;

#define VERIFY(statement) \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return nullptr;
#define COMPARE(actual, expected) \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return nullptr;

    surface_holder = Test::create_surface();
    VERIFY(surface_holder.get());
    toplevel_holder = Test::create_xdg_shell_toplevel(surface_holder);
    VERIFY(toplevel_holder.get());

    // Let's render.
    auto c = Test::render_and_wait_for_shown(surface_holder, QSize(100, 50), Qt::blue);

    VERIFY(c);
    COMPARE(workspace()->activeClient(), c);

#undef VERIFY
#undef COMPARE

    return c;
}

void LockScreenTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());

    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    kwinApp()->start();

    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));

    setenv("QT_QPA_PLATFORM", "wayland", true);
    waylandServer()->initWorkspace();

    auto scene = KWin::Compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGL2Compositing);
}

void LockScreenTest::init()
{
    Test::setup_wayland_connection(Test::AdditionalWaylandInterface::Seat);
    QVERIFY(Test::wait_for_wayland_pointer());

    m_connection = Test::get_client().connection;
    m_compositor = Test::get_client().interfaces.compositor.get();
    m_shm = Test::get_client().interfaces.shm.get();
    m_seat = Test::get_client().interfaces.seat.get();

    screens()->setCurrent(0);
    Cursor::setPos(QPoint(640, 512));
}

void LockScreenTest::cleanup()
{
    toplevel_holder.reset();
    surface_holder.reset();
    Test::destroy_wayland_connection();
}

void LockScreenTest::testStackingOrder()
{
    // This test verifies that the lockscreen greeter is placed above other windows.

    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(clientAddedSpy.isValid());

    LOCK
    QVERIFY(clientAddedSpy.wait());

    auto client = clientAddedSpy.first().first().value<Toplevel*>();
    QVERIFY(client);
    QVERIFY(client->isLockScreen());
    QCOMPARE(client->layer(), win::layer::unmanaged);

    UNLOCK
}

void LockScreenTest::testPointer()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Pointer> pointer(m_seat->createPointer());
    QVERIFY(pointer);

    QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
    QSignalSpy leftSpy(pointer.get(), &Pointer::left);
    QVERIFY(leftSpy.isValid());
    QVERIFY(enteredSpy.isValid());

    auto c = showWindow();
    QVERIFY(c);

    // First move cursor into the center of the window.
    quint32 timestamp = 1;
    MOTION(c->frameGeometry().center());
    QVERIFY(enteredSpy.wait());

    LOCK

    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.count(), 1);

    // Simulate moving out in and out again.
    MOTION(c->frameGeometry().center());
    MOTION(c->frameGeometry().bottomRight() + QPoint(100, 100));
    MOTION(c->frameGeometry().bottomRight() + QPoint(100, 100));
    QVERIFY(!leftSpy.wait(500));
    QCOMPARE(leftSpy.count(), 1);
    QCOMPARE(enteredSpy.count(), 1);

    // Go back on the window.
    MOTION(c->frameGeometry().center());

    // And unlock.
    UNLOCK
    QTRY_COMPARE(enteredSpy.count(), 2);

    // Move on the window.
    MOTION(c->frameGeometry().center() + QPoint(100, 100));
    QVERIFY(leftSpy.wait());
    MOTION(c->frameGeometry().center());
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.count(), 3);
}

void LockScreenTest::testPointerButton()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Pointer> pointer(m_seat->createPointer());
    QVERIFY(pointer);

    QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
    QSignalSpy buttonChangedSpy(pointer.get(), &Pointer::buttonStateChanged);
    QVERIFY(enteredSpy.isValid());
    QVERIFY(buttonChangedSpy.isValid());

    auto c = showWindow();
    QVERIFY(c);

    // First move cursor into the center of the window.
    quint32 timestamp = 1;
    MOTION(c->frameGeometry().center());
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

void LockScreenTest::testPointerAxis()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Pointer> pointer(m_seat->createPointer());
    QVERIFY(pointer);

    QSignalSpy axisChangedSpy(pointer.get(), &Pointer::axisChanged);
    QSignalSpy enteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(axisChangedSpy.isValid());
    QVERIFY(enteredSpy.isValid());

    auto c = showWindow();
    QVERIFY(c);

    // First move cursor into the center of the window.
    quint32 timestamp = 1;
    MOTION(c->frameGeometry().center());
    QVERIFY(enteredSpy.wait());

    // And simulate axis.
    kwinApp()->platform()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());

    LOCK

    // Simulate axis one more time. Now without change.
    kwinApp()->platform()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(!axisChangedSpy.wait(500));
    kwinApp()->platform()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(!axisChangedSpy.wait(500));

    // And unlock.
    UNLOCK
    QTRY_COMPARE(enteredSpy.count(), 2);

    // And move axis again.
    kwinApp()->platform()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());
    kwinApp()->platform()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());
}

void LockScreenTest::testKeyboard()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Keyboard> keyboard(m_seat->createKeyboard());
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
    QCOMPARE(keyChangedSpy.at(0).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Pressed);
    QCOMPARE(keyChangedSpy.at(0).at(2).value<quint32>(), quint32(1));

    KEYRELEASE(KEY_A);
    QVERIFY(keyChangedSpy.wait());
    QCOMPARE(keyChangedSpy.count(), 2);
    QCOMPARE(keyChangedSpy.at(1).at(0).value<quint32>(), quint32(KEY_A));
    QCOMPARE(keyChangedSpy.at(1).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Released);
    QCOMPARE(keyChangedSpy.at(1).at(2).value<quint32>(), quint32(2));

    LOCK
    QVERIFY(leftSpy.wait());

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
    QCOMPARE(keyChangedSpy.at(2).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Pressed);
    QCOMPARE(keyChangedSpy.at(3).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Released);
}

void LockScreenTest::testScreenEdge()
{
    QSignalSpy screenEdgeSpy(ScreenEdges::self(), &ScreenEdges::approaching);
    QVERIFY(screenEdgeSpy.isValid());
    QCOMPARE(screenEdgeSpy.count(), 0);

    quint32 timestamp = 1;
    MOTION(QPoint(5, 5));
    QCOMPARE(screenEdgeSpy.count(), 1);

    LOCK
    MOTION(QPoint(4, 4));
    QCOMPARE(screenEdgeSpy.count(), 1);

    UNLOCK
    MOTION(QPoint(5, 5));
    QCOMPARE(screenEdgeSpy.count(), 2);
}

void LockScreenTest::testEffects()
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

    LOCK
    MOTION(QPoint(6, 6));
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

void LockScreenTest::testEffectsKeyboard()
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

    LOCK
    KEYPRESS(KEY_B);
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

void LockScreenTest::testEffectsKeyboardAutorepeat()
{
    // This test is just like testEffectsKeyboard, but tests auto repeat key events
    // while the key is pressed the Effect should get auto repeated events
    // but the lock screen should filter them out.

    std::unique_ptr<HelperEffect> effect(new HelperEffect);
    QSignalSpy inputSpy(effect.get(), &HelperEffect::keyEvent);
    QVERIFY(inputSpy.isValid());

    effects->grabKeyboard(effect.get());

    // We need to configure the key repeat first. It is only enabled on libinput.
    waylandServer()->seat()->setKeyRepeatInfo(25, 300);

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
    LOCK
    KEYPRESS(KEY_B);
    QVERIFY(!inputSpy.wait(500));
    KEYRELEASE(KEY_B);
    QVERIFY(!inputSpy.wait(500));

    // Don't test again, that's covered by testEffectsKeyboard.
    UNLOCK

    effects->ungrabKeyboard();
}

void LockScreenTest::testMoveWindow()
{
    using namespace Wrapland::Client;

    auto c = showWindow();
    QVERIFY(c);

    QSignalSpy clientStepUserMovedResizedSpy(c, &Toplevel::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    quint32 timestamp = 1;

    workspace()->slotWindowMove();
    QCOMPARE(workspace()->moveResizeClient(), c);
    QVERIFY(win::is_move(c));

    kwinApp()->platform()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QEXPECT_FAIL("", "First event is ignored", Continue);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    // TODO: Adjust once the expected fail is fixed.
    kwinApp()->platform()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    // While locking our window should continue to be in move resize.
    LOCK
    QCOMPARE(workspace()->moveResizeClient(), c);
    QVERIFY(win::is_move(c));
    kwinApp()->platform()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    UNLOCK
    QCOMPARE(workspace()->moveResizeClient(), c);
    QVERIFY(win::is_move(c));

    kwinApp()->platform()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);

    kwinApp()->platform()->keyboardKeyPressed(KEY_ESC, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_ESC, timestamp++);
    QVERIFY(!win::is_move(c));
}

void LockScreenTest::testPointerShortcut()
{
    using namespace Wrapland::Client;

    std::unique_ptr<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.get(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());

    input_redirect()->registerPointerShortcut(Qt::MetaModifier, Qt::LeftButton, action.get());

    // Try to trigger the shortcut.
    quint32 timestamp = 1;

#define PERFORM(expectedCount) \
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++); \
    PRESS; \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount); \
    RELEASE; \
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount);

    PERFORM(1)

    // Now the same thing with a locked screen.
    LOCK
    PERFORM(1)

    // And as unlocked.
    UNLOCK
    PERFORM(2)
#undef PERFORM
}


void LockScreenTest::testAxisShortcut_data()
{
    QTest::addColumn<Qt::Orientation>("direction");
    QTest::addColumn<int>("sign");

    QTest::newRow("up") << Qt::Vertical << 1;
    QTest::newRow("down") << Qt::Vertical << -1;
    QTest::newRow("left") << Qt::Horizontal << 1;
    QTest::newRow("right") << Qt::Horizontal << -1;
}

void LockScreenTest::testAxisShortcut()
{
    using namespace Wrapland::Client;

    std::unique_ptr<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.get(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());

    QFETCH(Qt::Orientation, direction);
    QFETCH(int, sign);

    PointerAxisDirection axisDirection = PointerAxisUp;

    if (direction == Qt::Vertical) {
        axisDirection = sign > 0 ? PointerAxisUp : PointerAxisDown;
    } else {
        axisDirection = sign > 0 ? PointerAxisLeft : PointerAxisRight;
    }

    input_redirect()->registerAxisShortcut(Qt::MetaModifier, axisDirection, action.get());

    // Try to trigger the shortcut.
    quint32 timestamp = 1;

#define PERFORM(expectedCount) \
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++); \
    if (direction == Qt::Vertical) \
        kwinApp()->platform()->pointerAxisVertical(sign * 5.0, timestamp++); \
    else \
        kwinApp()->platform()->pointerAxisHorizontal(sign * 5.0, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount); \
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount);

    PERFORM(1)

    // Now the same thing with a locked screen.
    LOCK
    PERFORM(1)

    // And as unlocked.
    UNLOCK
    PERFORM(2)

#undef PERFORM
}

void LockScreenTest::testKeyboardShortcut()
{
    using namespace Wrapland::Client;

    std::unique_ptr<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.get(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());

    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName("LockScreenTest::testKeyboardShortcut");

    KGlobalAccel::self()->setDefaultShortcut(action.get(),
                                QList<QKeySequence>{Qt::CTRL + Qt::META + Qt::ALT + Qt::Key_Space});
    KGlobalAccel::self()->setShortcut(action.get(),
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

    LOCK
    KEYPRESS(KEY_SPACE);
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

void LockScreenTest::testTouch()
{
    using namespace Wrapland::Client;

    auto *touch = m_seat->createTouch(m_seat);
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

    kwinApp()->platform()->touchDown(1, QPointF(25, 25), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);

    LOCK
    QVERIFY(cancelSpy.wait());

    kwinApp()->platform()->touchUp(1, timestamp++);

    QVERIFY(!pointRemovedSpy.wait(500));
    kwinApp()->platform()->touchDown(1, QPointF(25, 25), timestamp++);
    kwinApp()->platform()->touchMotion(1, QPointF(26, 26), timestamp++);
    kwinApp()->platform()->touchUp(1, timestamp++);

    UNLOCK
    kwinApp()->platform()->touchDown(1, QPointF(25, 25), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 2);

    kwinApp()->platform()->touchUp(1, timestamp++);
    QVERIFY(pointRemovedSpy.wait());
    QCOMPARE(pointRemovedSpy.count(), 1);
}

}

WAYLANDTEST_MAIN(KWin::LockScreenTest)
#include "lockscreen.moc"
