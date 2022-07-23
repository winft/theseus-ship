/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "win/active_window.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/meta.h"
#include "win/shortcut_dialog.h"
#include "win/space.h"
#include "win/user_actions_menu.h"
#include "win/x11/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <KGlobalAccel>
#include <linux/input.h>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

Q_DECLARE_METATYPE(Qt::Modifier)

using namespace Wrapland::Client;

namespace KWin
{

class GlobalShortcutsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testNonLatinLayout_data();
    void testNonLatinLayout();
    void testConsumedShift();
    void testRepeatedTrigger();
    void testUserActionsMenu();
    void testMetaShiftW();
    void testComponseKey();
    void testX11ClientShortcut();
    void testWaylandClientShortcut();
    void testSetupWindowShortcut();
};

void GlobalShortcutsTest::initTestCase()
{
    qRegisterMetaType<win::internal_window*>();
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");
    qputenv("XKB_DEFAULT_LAYOUT", "us,ru");

    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void GlobalShortcutsTest::init()
{
    Test::setup_wayland_connection();
    input::get_cursor()->set_pos(QPoint(640, 512));

    input::xkb::get_primary_xkb_keyboard()->switch_to_layout(0);
}

void GlobalShortcutsTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void GlobalShortcutsTest::testNonLatinLayout_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<Qt::Modifier>("qtModifier");
    QTest::addColumn<int>("key");
    QTest::addColumn<Qt::Key>("qtKey");

    for (const auto& modifier : QVector<QPair<int, Qt::Modifier>>{
             {KEY_LEFTCTRL, Qt::CTRL},
             {KEY_LEFTALT, Qt::ALT},
             {KEY_LEFTSHIFT, Qt::SHIFT},
             {KEY_LEFTMETA, Qt::META},
         }) {
        for (const auto& key : QVector<QPair<int, Qt::Key>> {
                 // Tab is example of a key usually the same on different layouts, check it first
                 {KEY_TAB, Qt::Key_Tab},

                     // Then check a key with a Latin letter.
                     // The symbol will probably be differ on non-Latin layout.
                     // On Russian layout, "w" key has a cyrillic letter "ц"
                     {KEY_W, Qt::Key_W},

#if QT_VERSION_MAJOR > 5 // since Qt 5 LTS is frozen
                     // More common case with any Latin1 symbol keys, including punctuation, should
                     // work also.
                     // "`" key has a "ё" letter on Russian layout
                     // FIXME: QTBUG-90611
                     {KEY_GRAVE, Qt::Key_QuoteLeft},
#endif
             }) {
            QTest::newRow(
                QKeySequence(modifier.second + key.second).toString().toLatin1().constData())
                << modifier.first << modifier.second << key.first << key.second;
        }
    }
}

void GlobalShortcutsTest::testNonLatinLayout()
{
    // Shortcuts on non-Latin layouts should still work, see BUG 375518
    auto xkb = input::xkb::get_primary_xkb_keyboard();
    xkb->switch_to_layout(1);
    QCOMPARE(xkb->layout_name(), "Russian");

    QFETCH(int, modifierKey);
    QFETCH(Qt::Modifier, qtModifier);
    QFETCH(int, key);
    QFETCH(Qt::Key, qtKey);

    const QKeySequence seq(qtModifier + qtKey);

    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName("globalshortcuts-test-non-latin-layout");

    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());

    KGlobalAccel::self()->stealShortcutSystemwide(seq);
    KGlobalAccel::self()->setShortcut(action.get(), {seq}, KGlobalAccel::NoAutoloading);
    kwinApp()->input->registerShortcut(seq, action.get());

    quint32 timestamp = 0;
    Test::keyboard_key_pressed(modifierKey, timestamp++);
    QCOMPARE(xkb->qt_modifiers, qtModifier);
    Test::keyboard_key_pressed(key, timestamp++);

    Test::keyboard_key_released(key, timestamp++);
    Test::keyboard_key_released(modifierKey, timestamp++);

    QTRY_COMPARE_WITH_TIMEOUT(triggeredSpy.count(), 1, 100);
}

void GlobalShortcutsTest::testConsumedShift()
{
    // this test verifies that a shortcut with a consumed shift modifier triggers
    // create the action
    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));
    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(
        action.get(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
    kwinApp()->input->registerShortcut(Qt::Key_Percent, action.get());

    // press shift+5
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::ShiftModifier);
    Test::keyboard_key_pressed(KEY_5, timestamp++);

    QVERIFY(triggeredSpy.size() || triggeredSpy.wait());
    QCOMPARE(triggeredSpy.size(), 1);

    Test::keyboard_key_released(KEY_5, timestamp++);

    // release shift
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
}

void GlobalShortcutsTest::testRepeatedTrigger()
{
    // this test verifies that holding a key, triggers repeated global shortcut
    // in addition pressing another key should stop triggering the shortcut

    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));
    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(
        action.get(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
    kwinApp()->input->registerShortcut(Qt::Key_Percent, action.get());

    // we need to configure the key repeat first. It is only enabled on libinput
    waylandServer()->seat()->keyboards().set_repeat_info(25, 300);

    // press shift+5
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_WAKEUP, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::ShiftModifier);
    Test::keyboard_key_pressed(KEY_5, timestamp++);

    QVERIFY(triggeredSpy.size() || triggeredSpy.wait());
    QCOMPARE(triggeredSpy.size(), 1);

    // and should repeat
    QVERIFY(triggeredSpy.wait());
    QVERIFY(triggeredSpy.wait());

    // now release the key
    Test::keyboard_key_released(KEY_5, timestamp++);
    QVERIFY(!triggeredSpy.wait(50));

    Test::keyboard_key_released(KEY_WAKEUP, timestamp++);
    QVERIFY(!triggeredSpy.wait(50));

    // release shift
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
}

void GlobalShortcutsTest::testUserActionsMenu()
{
    // this test tries to trigger the user actions menu with Alt+F3
    // the problem here is that pressing F3 consumes modifiers as it's part of the
    // Ctrl+alt+F3 keysym for vt switching. xkbcommon considers all modifiers as consumed
    // which a transformation to any keysym would cause
    // for more information see:
    // https://bugs.freedesktop.org/show_bug.cgi?id=92818
    // https://github.com/xkbcommon/libxkbcommon/issues/17

    // first create a window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());

    quint32 timestamp = 0;
    QVERIFY(!Test::app()->base.space->user_actions_menu->isShown());
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_F3, timestamp++);
    Test::keyboard_key_released(KEY_F3, timestamp++);
    QTRY_VERIFY(Test::app()->base.space->user_actions_menu->isShown());
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
}

void GlobalShortcutsTest::testMetaShiftW()
{
    // BUG 370341
    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-meta-shift-w"));
    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.get(),
                                      QList<QKeySequence>{Qt::META + Qt::SHIFT + Qt::Key_W},
                                      KGlobalAccel::NoAutoloading);
    kwinApp()->input->registerShortcut(Qt::META + Qt::SHIFT + Qt::Key_W, action.get());

    // press meta+shift+w
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::MetaModifier);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input),
             Qt::ShiftModifier | Qt::MetaModifier);
    Test::keyboard_key_pressed(KEY_W, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
    Test::keyboard_key_released(KEY_W, timestamp++);

    // release meta+shift
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
}

void GlobalShortcutsTest::testComponseKey()
{
    // BUG 390110
    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-accent"));
    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(
        action.get(), QList<QKeySequence>{Qt::UNICODE_ACCEL}, KGlobalAccel::NoAutoloading);
    kwinApp()->input->registerShortcut(Qt::UNICODE_ACCEL, action.get());

    // press & release `
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_RESERVED, timestamp++);
    Test::keyboard_key_released(KEY_RESERVED, timestamp++);

    QTRY_COMPARE(triggeredSpy.count(), 0);
}

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

void GlobalShortcutsTest::testX11ClientShortcut()
{
#ifdef NO_XWAYLAND
    QSKIP("x11 test, unnecessary without xwayland");
#endif
    // create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_EVENT_MASK,
                      values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client);

    QCOMPARE(Test::app()->base.space->active_client, client);
    QVERIFY(client->control->active());
    QCOMPARE(client->control->shortcut(), QKeySequence());
    const QKeySequence seq(Qt::META + Qt::SHIFT + Qt::Key_Y);
    QVERIFY(win::shortcut_available(*Test::app()->base.space, seq, nullptr));
    win::set_shortcut(client, seq.toString());
    QCOMPARE(client->control->shortcut(), seq);
    QVERIFY(!win::shortcut_available(*Test::app()->base.space, seq, nullptr));
    QCOMPARE(win::caption(client), QStringLiteral(" {Meta+Shift+Y}"));

    // it's delayed
    QCoreApplication::processEvents();

    win::activate_window(*Test::app()->base.space, nullptr);
    QVERIFY(!Test::app()->base.space->active_client);
    QVERIFY(!client->control->active());

    // now let's trigger the shortcut
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_pressed(KEY_Y, timestamp++);
    QTRY_COMPARE(Test::app()->base.space->active_client, client);
    Test::keyboard_key_released(KEY_Y, timestamp++);
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    // destroy window again
    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

void GlobalShortcutsTest::testWaylandClientShortcut()
{
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QCOMPARE(Test::app()->base.space->active_client, client);
    QVERIFY(client->control->active());
    QCOMPARE(client->control->shortcut(), QKeySequence());
    const QKeySequence seq(Qt::META + Qt::SHIFT + Qt::Key_Y);
    QVERIFY(win::shortcut_available(*Test::app()->base.space, seq, nullptr));
    win::set_shortcut(client, seq.toString());
    QCOMPARE(client->control->shortcut(), seq);
    QVERIFY(!win::shortcut_available(*Test::app()->base.space, seq, nullptr));
    QCOMPARE(win::caption(client), QStringLiteral(" {Meta+Shift+Y}"));

    win::activate_window(*Test::app()->base.space, nullptr);
    QVERIFY(!Test::app()->base.space->active_client);
    QVERIFY(!client->control->active());

    // now let's trigger the shortcut
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_pressed(KEY_Y, timestamp++);
    QTRY_COMPARE(Test::app()->base.space->active_client, client);
    Test::keyboard_key_released(KEY_Y, timestamp++);
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));

    // Wait a bit for KGlobalAccel to catch up.
    QTest::qWait(100);
    QVERIFY(win::shortcut_available(*Test::app()->base.space, seq, nullptr));
}

void GlobalShortcutsTest::testSetupWindowShortcut()
{
    // QTBUG-62102

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QCOMPARE(Test::app()->base.space->active_client, client);
    QVERIFY(client->control->active());
    QCOMPARE(client->control->shortcut(), QKeySequence());

    QSignalSpy shortcutDialogAddedSpy(Test::app()->base.space->qobject.get(),
                                      &win::space::qobject_t::internalClientAdded);
    QVERIFY(shortcutDialogAddedSpy.isValid());
    win::active_window_setup_window_shortcut(*Test::app()->base.space);
    QTRY_COMPARE(shortcutDialogAddedSpy.count(), 1);
    auto dialog = shortcutDialogAddedSpy.first().first().value<win::internal_window*>();
    QVERIFY(dialog);
    QVERIFY(dialog->isInternal());
    auto sequenceEdit = Test::app()->base.space->client_keys_dialog->findChild<QKeySequenceEdit*>();
    QVERIFY(sequenceEdit);

    // the QKeySequenceEdit field does not get focus, we need to pass it focus manually
    QEXPECT_FAIL("", "Edit does not have focus", Continue);
    QVERIFY(sequenceEdit->hasFocus());
    sequenceEdit->setFocus();
    QTRY_VERIFY(sequenceEdit->hasFocus());

    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_pressed(KEY_Y, timestamp++);
    Test::keyboard_key_released(KEY_Y, timestamp++);
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    // the sequence gets accepted after one second, so wait a bit longer
    QTest::qWait(2000);
    // now send in enter
    Test::keyboard_key_pressed(KEY_ENTER, timestamp++);
    Test::keyboard_key_released(KEY_ENTER, timestamp++);
    QTRY_COMPARE(client->control->shortcut(), QKeySequence(Qt::META + Qt::SHIFT + Qt::Key_Y));
}

}

WAYLANDTEST_MAIN(KWin::GlobalShortcutsTest)
#include "globalshortcuts_test.moc"
