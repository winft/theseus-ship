/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "input/cursor.h"
#include "win/activation.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <linux/input.h>
#include <memory>

namespace KWin
{

using wayland_window = win::wayland::window;

struct test_window {
    test_window() = default;
    test_window(test_window const&) = delete;
    test_window& operator=(test_window const&) = delete;
    test_window(test_window&& other) noexcept;
    test_window& operator=(test_window&& other) noexcept;
    ~test_window()
    {
        client_toplevel.reset();
        if (window) {
            QSignalSpy windowDeletedSpy(window, &Toplevel::closed);
            QVERIFY(windowDeletedSpy.isValid());
            QVERIFY(Test::wait_for_destroyed(window));
            QCOMPARE(windowDeletedSpy.size(), 1);
        }
    }
    std::unique_ptr<Wrapland::Client::Surface> client_surface;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
    wayland_window* window{nullptr};
};

class keyboard_keymap_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_focus();
};

void keyboard_keymap_test::initTestCase()
{
    qRegisterMetaType<Toplevel*>();
    qRegisterMetaType<Wrapland::Client::Output*>();
    qRegisterMetaType<Wrapland::Client::Keyboard::KeyState>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

test_window create_window(Test::client& client)
{
    test_window ret;
    ret.client_surface = Test::create_surface(client);
    ret.client_toplevel = Test::create_xdg_shell_toplevel(client, ret.client_surface);
    ret.window
        = Test::render_and_wait_for_shown(client, ret.client_surface, QSize(1280, 1024), Qt::red);
    return ret;
}

Test::client create_focus_client()
{
    return Test::client(Test::global_selection::seat);
}

std::string create_keymap()
{
    auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    auto const model = "pc104";
    auto const layout = "de";
    auto const variant = "nodeadkeys";
    auto const options = "";

    auto const rule_names = xkb_rule_names{
        .rules = nullptr, .model = model, .layout = layout, .variant = variant, .options = options};

    auto keymap = xkb_keymap_new_from_names(context, &rule_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    auto keymap_c_string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    auto keymap_string = std::string(keymap_c_string);

    free(keymap_c_string);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    return keymap_string;
}

void keyboard_keymap_test::init()
{
    Test::app()->base.input->cursor->set_pos(QPoint(512, 512));
}

void keyboard_keymap_test::cleanup()
{
    // Make sure we animate.
    QTest::qWait(1000);
    QVERIFY(Test::app()->base.space->windows.empty());

    Test::destroy_wayland_connection();
}

/**
 * Create an input method popup and a text-input client afterwards.
 * Verify that the popup is drawn with acceptable geometry and the window is destroyed on release.
 */
void keyboard_keymap_test::test_focus()
{
    auto focus_client1 = create_focus_client();
    QVERIFY(!focus_client1.interfaces.seat->hasKeyboard());

    QSignalSpy client_keyboard_spy(focus_client1.interfaces.seat.get(),
                                   &Wrapland::Client::Seat::hasKeyboardChanged);
    QVERIFY(client_keyboard_spy.isValid());
    QVERIFY(client_keyboard_spy.wait());

    auto keyboard = std::unique_ptr<Wrapland::Client::Keyboard>(
        focus_client1.interfaces.seat->createKeyboard());

    QSignalSpy client1_keymap_spy(keyboard.get(), &Wrapland::Client::Keyboard::keymapChanged);
    QVERIFY(client1_keymap_spy.isValid());

    auto window1 = create_window(focus_client1);
    QCOMPARE(Test::app()->base.space->active_client, window1.window);

    // After focus we don't yet get the current keymap as none was set yet.
    QVERIFY(!client1_keymap_spy.wait(500));
    QCOMPARE(client1_keymap_spy.size(), 0);

    // Now we press some key, so we get the current keymap.
    uint32_t timestamp{0};
    Test::keyboard_key_pressed(KEY_E, timestamp++);
    Test::keyboard_key_released(KEY_E, timestamp++);
    QVERIFY(client1_keymap_spy.wait());
    QCOMPARE(client1_keymap_spy.size(), 1);

    // On a second window with focus we now directly get the current keymap.
    auto focus_client2 = create_focus_client();
    auto window2 = create_window(focus_client2);
    QCOMPARE(Test::app()->base.space->active_client, window2.window);

    auto keyboard2 = std::unique_ptr<Wrapland::Client::Keyboard>(
        focus_client2.interfaces.seat->createKeyboard());

    QSignalSpy client2_keymap_spy(keyboard2.get(), &Wrapland::Client::Keyboard::keymapChanged);
    QVERIFY(client2_keymap_spy.isValid());
    QVERIFY(client2_keymap_spy.wait());
    QCOMPARE(client1_keymap_spy.size(), 1);
    QCOMPARE(client2_keymap_spy.size(), 1);

    // We switch back and don't get a new keymap.
    win::activate_window(*Test::app()->base.space, window1.window);
    QCOMPARE(Test::app()->base.space->active_client, window1.window);

    QVERIFY(!client1_keymap_spy.wait(500));
    QCOMPARE(client1_keymap_spy.size(), 1);
    QCOMPARE(client2_keymap_spy.size(), 1);
}

}

WAYLANDTEST_MAIN(KWin::keyboard_keymap_test)
#include "keyboard_keymap.moc"
