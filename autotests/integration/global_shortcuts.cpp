/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

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
#include <QKeySequenceEdit>
#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

namespace
{

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

}

namespace KWin::detail::test
{

TEST_CASE("global shortcuts", "[input]")
{
    test::setup setup("global-shortcuts", base::operation_mode::xwayland);

    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");
    qputenv("XKB_DEFAULT_LAYOUT", "us,ru");

    setup.start();

    setup_wayland_connection();
    cursor()->set_pos(QPoint(640, 512));

    input::xkb::get_primary_xkb_keyboard(*setup.base->input)->switch_to_layout(0);

    SECTION("non-latin layout")
    {
        // Shortcuts on non-Latin layouts should still work, see BUG 375518

        struct mod_data {
            int key;
            Qt::Modifier qt;
        };

        struct key_data {
            int key;
            Qt::Key qt;
        };

        auto mod_test_data = GENERATE(mod_data{KEY_LEFTCTRL, Qt::CTRL},
                                      mod_data{KEY_LEFTALT, Qt::ALT},
                                      mod_data{KEY_LEFTSHIFT, Qt::SHIFT},
                                      mod_data{KEY_LEFTMETA, Qt::META});

        auto key_test_data = GENERATE(
            // Tab is example of a key usually the same on different layouts, check it first.
            key_data{KEY_TAB, Qt::Key_Tab},
#if 1
            // Then check a key with a Latin letter. The symbol will probably be differ on non-Latin
            // layout. On Russian layout, "w" key has a cyrillic letter "ц".
            key_data{KEY_W, Qt::Key_W});
#else
            key_data{KEY_W, Qt::Key_W},
            // TODO(romangg): grave key is still not working with Qt6.
            // More common case with any Latin1 symbol keys, including punctuation,
            // should work also. "`" key has a "ё" letter on Russian layout.
            // FIXME: QTBUG-90611
            key_data{KEY_GRAVE, Qt::Key_QuoteLeft});
#endif

        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        xkb->switch_to_layout(1);
        QCOMPARE(xkb->layout_name(), "Russian");

        QKeySequence const seq(mod_test_data.qt | key_test_data.qt);

        auto action = std::make_unique<QAction>();
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName("globalshortcuts-test-non-latin-layout");

        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());

        KGlobalAccel::self()->stealShortcutSystemwide(seq);
        KGlobalAccel::self()->setShortcut(action.get(), {seq}, KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(seq, action.get());

        quint32 timestamp = 0;
        keyboard_key_pressed(mod_test_data.key, timestamp++);
        QCOMPARE(xkb->qt_modifiers, mod_test_data.qt);

        keyboard_key_pressed(key_test_data.key, timestamp++);

        keyboard_key_released(key_test_data.key, timestamp++);
        keyboard_key_released(mod_test_data.key, timestamp++);

        TRY_REQUIRE(triggeredSpy.size() == 1);
    }

    SECTION("consumed shift")
    {
        // Verifies that a shortcut with a consumed shift modifier triggers create the action.
        auto action = std::make_unique<QAction>();
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));

        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());

        KGlobalAccel::self()->setShortcut(
            action.get(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(Qt::Key_Percent, action.get());

        // press shift+5
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::ShiftModifier);
        keyboard_key_pressed(KEY_5, timestamp++);

        REQUIRE(triggeredSpy.wait());

        keyboard_key_released(KEY_5, timestamp++);

        // release shift
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    }

    SECTION("repeated trigger")
    {
        // Verifies that holding a key, triggers repeated global shortcut in addition pressing
        // another key should stop triggering the shortcut.

        auto action = std::make_unique<QAction>();
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));

        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());

        KGlobalAccel::self()->setShortcut(
            action.get(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(Qt::Key_Percent, action.get());

        // we need to configure the key repeat first. It is only enabled on libinput
        setup.base->server->seat()->keyboards().set_repeat_info(25, 300);

        // press shift+5
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_WAKEUP, timestamp++);
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::ShiftModifier);
        keyboard_key_pressed(KEY_5, timestamp++);

        REQUIRE(triggeredSpy.wait());

        // and should repeat
        QVERIFY(triggeredSpy.wait());
        QVERIFY(triggeredSpy.wait());

        // now release the key
        keyboard_key_released(KEY_5, timestamp++);
        QVERIFY(!triggeredSpy.wait(50));

        keyboard_key_released(KEY_WAKEUP, timestamp++);
        QVERIFY(!triggeredSpy.wait(50));

        // release shift
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    }

    SECTION("user actions menu")
    {
        // Tries to trigger the user actions menu with Alt+F3. The problem here is that pressing F3
        // consumes modifiers as it's part of the Ctrl+alt+F3 keysym for vt switching. xkbcommon
        // considers all modifiers as consumed which a transformation to any keysym would cause for
        // more information see:
        //   * https://bugs.freedesktop.org/show_bug.cgi?id=92818
        //   * https://github.com/xkbcommon/libxkbcommon/issues/17

        // first create a window
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QVERIFY(c->control->active);

        quint32 timestamp = 0;
        QVERIFY(!setup.base->space->user_actions_menu->isShown());
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_F3, timestamp++);
        keyboard_key_released(KEY_F3, timestamp++);
        QTRY_VERIFY(setup.base->space->user_actions_menu->isShown());
        keyboard_key_released(KEY_LEFTALT, timestamp++);
    }

    SECTION("meta-shift-w")
    {
        // BUG 370341
        auto action = std::make_unique<QAction>();
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName(QStringLiteral("globalshortcuts-test-meta-shift-w"));
        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());
        KGlobalAccel::self()->setShortcut(action.get(),
                                          QList<QKeySequence>{Qt::META | Qt::SHIFT | Qt::Key_W},
                                          KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(Qt::META | Qt::SHIFT | Qt::Key_W, action.get());

        // press meta+shift+w
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::MetaModifier);
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        REQUIRE(input::xkb::get_active_keyboard_modifiers(*setup.base->input)
                == (Qt::ShiftModifier | Qt::MetaModifier));
        keyboard_key_pressed(KEY_W, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 1);
        keyboard_key_released(KEY_W, timestamp++);

        // release meta+shift
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
    }

    SECTION("accent")
    {
        // BUG 390110
        auto action = std::make_unique<QAction>();
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName(QStringLiteral("globalshortcuts-accent"));

        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());

        KGlobalAccel::self()->setShortcut(
            action.get(), QList<QKeySequence>{Qt::NoModifier}, KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(Qt::NoModifier, action.get());

        // press & release
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_RESERVED, timestamp++);
        keyboard_key_released(KEY_RESERVED, timestamp++);

        QTRY_COMPARE(triggeredSpy.count(), 0);
    }

    SECTION("x11 window shortcut")
    {
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry = QRect(0, 0, 10, 20);
        const uint32_t values[] = {XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
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
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::normal);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.last().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);

        QCOMPARE(get_x11_window(setup.base->space->stacking.active), client);
        QVERIFY(client->control->active);
        QCOMPARE(client->control->shortcut, QKeySequence());
        const QKeySequence seq(Qt::META | Qt::SHIFT | Qt::Key_Y);
        QVERIFY(win::shortcut_available(
            *setup.base->space, seq, static_cast<wayland_window*>(nullptr)));
        win::set_shortcut(client, seq.toString());
        QCOMPARE(client->control->shortcut, seq);
        QVERIFY(!win::shortcut_available(
            *setup.base->space, seq, static_cast<wayland_window*>(nullptr)));
        QCOMPARE(win::caption(client), QStringLiteral(" {Meta+Shift+Y}"));

        // it's delayed
        QCoreApplication::processEvents();

        win::deactivate_window(*setup.base->space);
        QVERIFY(!setup.base->space->stacking.active);
        QVERIFY(!client->control->active);

        // now let's trigger the shortcut
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_pressed(KEY_Y, timestamp++);
        QTRY_COMPARE(get_x11_window(setup.base->space->stacking.active), client);
        keyboard_key_released(KEY_Y, timestamp++);
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);

        // destroy window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("wayland window shortcut")
    {
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), client);
        QVERIFY(client->control->active);
        QCOMPARE(client->control->shortcut, QKeySequence());

        const QKeySequence seq(Qt::META | Qt::SHIFT | Qt::Key_Y);
        QVERIFY(win::shortcut_available(
            *setup.base->space, seq, static_cast<wayland_window*>(nullptr)));

        win::set_shortcut(client, seq.toString());
        QCOMPARE(client->control->shortcut, seq);
        QVERIFY(!win::shortcut_available(
            *setup.base->space, seq, static_cast<wayland_window*>(nullptr)));
        QCOMPARE(win::caption(client), QStringLiteral(" {Meta+Shift+Y}"));

        win::deactivate_window(*setup.base->space);
        QVERIFY(!setup.base->space->stacking.active);
        QVERIFY(!client->control->active);

        // now let's trigger the shortcut
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_pressed(KEY_Y, timestamp++);
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), client);
        keyboard_key_released(KEY_Y, timestamp++);
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);

        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));

        // Wait a bit for KGlobalAccel to catch up.
        QTest::qWait(100);
        QVERIFY(win::shortcut_available(
            *setup.base->space, seq, static_cast<wayland_window*>(nullptr)));
    }

    SECTION("setup window shortcut")
    {
        // QTBUG-62102

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), client);
        QVERIFY(client->control->active);
        QCOMPARE(client->control->shortcut, QKeySequence());

        QSignalSpy shortcutDialogAddedSpy(setup.base->space->qobject.get(),
                                          &space::qobject_t::internalClientAdded);
        QVERIFY(shortcutDialogAddedSpy.isValid());
        win::active_window_setup_window_shortcut(*setup.base->space);
        QTRY_COMPARE(shortcutDialogAddedSpy.count(), 1);

        auto dialog_signal_id = shortcutDialogAddedSpy.first().first().value<quint32>();
        auto dialog = get_internal_window(setup.base->space->windows_map.at(dialog_signal_id));
        QVERIFY(dialog);
        QVERIFY(dialog->isInternal());
        auto sequenceEdit = setup.base->space->client_keys_dialog->findChild<QKeySequenceEdit*>();
        QVERIFY(sequenceEdit);
        QVERIFY(sequenceEdit->hasFocus());

        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_pressed(KEY_Y, timestamp++);
        keyboard_key_released(KEY_Y, timestamp++);
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);

        // the sequence gets accepted after one second, so wait a bit longer
        QTest::qWait(2000);
        // now send in enter
        keyboard_key_pressed(KEY_ENTER, timestamp++);
        keyboard_key_released(KEY_ENTER, timestamp++);
        QTRY_COMPARE(client->control->shortcut, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_Y));
    }
}

}
