/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "input/cursor.h"
#include "win/activation.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <linux/input.h>
#include <memory>

namespace KWin::detail::test
{

namespace
{

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
            QSignalSpy windowDeletedSpy(window->qobject.get(), &win::window_qobject::closed);
            QVERIFY(windowDeletedSpy.isValid());
            QVERIFY(wait_for_destroyed(window));
            QCOMPARE(windowDeletedSpy.size(), 1);
        }
    }
    std::unique_ptr<Wrapland::Client::Surface> client_surface;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
    wayland_window* window{nullptr};
};

}

TEST_CASE("keyboard keymap", "[input]")
{
    test::setup setup("keyboard-keymap");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(512, 512));

    auto create_window = [](client const& client) {
        test_window ret;
        ret.client_surface = create_surface(client);
        ret.client_toplevel = create_xdg_shell_toplevel(client, ret.client_surface);
        ret.window
            = render_and_wait_for_shown(client, ret.client_surface, QSize(1280, 1024), Qt::red);
        return ret;
    };

    auto create_focus_client = []() { return client(global_selection::seat); };

    auto create_keymap = []() -> std::string {
        auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        auto const model = "pc104";
        auto const layout = "de";
        auto const variant = "nodeadkeys";
        auto const options = "";

        auto const rule_names = xkb_rule_names{.rules = nullptr,
                                               .model = model,
                                               .layout = layout,
                                               .variant = variant,
                                               .options = options};

        auto keymap = xkb_keymap_new_from_names(context, &rule_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        auto keymap_c_string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        auto keymap_string = std::string(keymap_c_string);

        free(keymap_c_string);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        return keymap_string;
    };

    SECTION("focus")
    {
        /**
         * Create an input method popup and a text-input client afterwards. Verify that the popup is
         * drawn with acceptable geometry and the window is destroyed on release.
         */
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
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1.window);

        // After focus we don't yet get the current keymap as none was set yet.
        QVERIFY(!client1_keymap_spy.wait(500));
        QCOMPARE(client1_keymap_spy.size(), 0);

        // Now we press some key, so we get the current keymap.
        uint32_t timestamp{0};
        keyboard_key_pressed(KEY_E, timestamp++);
        keyboard_key_released(KEY_E, timestamp++);
        QVERIFY(client1_keymap_spy.wait());
        QCOMPARE(client1_keymap_spy.size(), 1);

        // On a second window with focus we now directly get the current keymap.
        auto focus_client2 = create_focus_client();
        auto window2 = create_window(focus_client2);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window2.window);

        auto keyboard2 = std::unique_ptr<Wrapland::Client::Keyboard>(
            focus_client2.interfaces.seat->createKeyboard());

        QSignalSpy client2_keymap_spy(keyboard2.get(), &Wrapland::Client::Keyboard::keymapChanged);
        QVERIFY(client2_keymap_spy.isValid());
        QVERIFY(client2_keymap_spy.wait());
        QCOMPARE(client1_keymap_spy.size(), 1);
        QCOMPARE(client2_keymap_spy.size(), 1);

        // We switch back and don't get a new keymap.
        win::activate_window(*setup.base->space, *window1.window);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1.window);

        QVERIFY(!client1_keymap_spy.wait(500));
        QCOMPARE(client1_keymap_spy.size(), 1);
        QCOMPARE(client2_keymap_spy.size(), 1);
    }
}

}
