/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/region.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/virtual_keyboard_v1.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/virtual_keyboard_v1.h>
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
    test_window(test_window&& other) noexcept = default;
    test_window& operator=(test_window&& other) noexcept = default;
    ~test_window() = default;

    std::unique_ptr<Wrapland::Client::Surface> client_surface;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
    wayland_window* window{nullptr};
};

std::unique_ptr<Wrapland::Client::virtual_keyboard_v1> create_virtual_keyboard(client& client)
{
    return std::unique_ptr<Wrapland::Client::virtual_keyboard_v1>(
        client.interfaces.virtual_keyboard_manager_v1->create_virtual_keyboard(
            client.interfaces.seat.get()));
}

}

TEST_CASE("virtual keyboard", "[input]")
{
    test::setup setup("virtual-keyboard");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();

    auto vk_client = client(global_selection::seat | global_selection::virtual_keyboard_manager_v1);
    auto focus_client = client(global_selection::seat);

    auto create_window = [](auto const& client) {
        test_window ret;
        ret.client_surface = create_surface(client);
        ret.client_toplevel = create_xdg_shell_toplevel(client, ret.client_surface);
        ret.window
            = render_and_wait_for_shown(client, ret.client_surface, QSize(1280, 1024), Qt::red);
        return std::move(ret);
    };

    auto create_keymap = []() {
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

    SECTION("keymap")
    {
        // Verifies that keymaps are correctly submitted and updated.

        QVERIFY(!focus_client.interfaces.seat->hasKeyboard());

        QSignalSpy client_keyboard_spy(focus_client.interfaces.seat.get(),
                                       &Wrapland::Client::Seat::hasKeyboardChanged);
        QVERIFY(client_keyboard_spy.isValid());
        QVERIFY(client_keyboard_spy.wait());

        auto keyboard = std::unique_ptr<Wrapland::Client::Keyboard>(
            focus_client.interfaces.seat->createKeyboard());

        QSignalSpy client_keymap_spy(keyboard.get(), &Wrapland::Client::Keyboard::keymapChanged);
        QVERIFY(client_keymap_spy.isValid());

        auto window = create_window(focus_client);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window.window);

        // After focus we don't yet get the current keymap as none was set yet.
        QVERIFY(!client_keymap_spy.wait(500));

        // Now we press some key, so we get the current keymap.
        uint32_t timestamp{0};
        keyboard_key_pressed(KEY_Y, timestamp++);
        keyboard_key_released(KEY_Y, timestamp++);
        QVERIFY(client_keymap_spy.wait());

        QSignalSpy vk_spy(setup.base->input->virtual_keyboard.get(),
                          &Wrapland::Server::virtual_keyboard_manager_v1::keyboard_created);
        QVERIFY(vk_spy.isValid());

        auto vk = create_virtual_keyboard(vk_client);

        QVERIFY(vk_spy.wait());
        auto server_vk = vk_spy.back().front().value<Wrapland::Server::virtual_keyboard_v1*>();

        // Need to set a keymap first.
        QSignalSpy vk_keymap_spy(server_vk, &Wrapland::Server::virtual_keyboard_v1::keymap);
        QVERIFY(vk_keymap_spy.isValid());

        auto keymap1 = create_keymap();
        vk->keymap(keymap1);
        QVERIFY(vk_keymap_spy.wait());

        // No change of keymap since the previous keyboard is still the actively used one.
        QVERIFY(!client_keymap_spy.wait(500));
        QCOMPARE(client_keymap_spy.size(), 1);

        // Now we press on the virtual keyboard and we should get the new new keymap.
        vk->key(
            std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::pressed);
        vk->key(
            std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::released);

        QVERIFY(client_keymap_spy.wait());
    }

    SECTION("keys")
    {
        // Verifies that keys are processed.
        QSignalSpy vk_spy(setup.base->input->virtual_keyboard.get(),
                          &Wrapland::Server::virtual_keyboard_manager_v1::keyboard_created);
        QVERIFY(vk_spy.isValid());

        auto vk = create_virtual_keyboard(vk_client);

        QVERIFY(vk_spy.wait());
        auto server_vk = vk_spy.back().front().value<Wrapland::Server::virtual_keyboard_v1*>();

        // Need to set a keymap first.
        QSignalSpy vk_keymap_spy(server_vk, &Wrapland::Server::virtual_keyboard_v1::keymap);
        QVERIFY(vk_keymap_spy.isValid());

        auto keymap1 = create_keymap();
        vk->keymap(keymap1);
        QVERIFY(vk_keymap_spy.wait());

        QVERIFY(!focus_client.interfaces.seat->hasKeyboard());

        QSignalSpy client_keyboard_spy(focus_client.interfaces.seat.get(),
                                       &Wrapland::Client::Seat::hasKeyboardChanged);
        QVERIFY(client_keyboard_spy.isValid());
        QVERIFY(client_keyboard_spy.wait());

        auto keyboard = std::unique_ptr<Wrapland::Client::Keyboard>(
            focus_client.interfaces.seat->createKeyboard());

        QSignalSpy key_spy(keyboard.get(), &Wrapland::Client::Keyboard::keyChanged);
        QVERIFY(key_spy.isValid());

        auto window = create_window(focus_client);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window.window);

        // Now we press on the virtual keyboard and we should get the new new keymap.
        int timestamp{0};
        vk->key(
            std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::pressed);
        QVERIFY(key_spy.wait());

        QCOMPARE(key_spy.back().at(0).toInt(), KEY_Y);
        QCOMPARE(key_spy.back().at(1).value<Wrapland::Client::Keyboard::KeyState>(),
                 Wrapland::Client::Keyboard::KeyState::Pressed);
        QCOMPARE(key_spy.back().at(2).toInt(), timestamp);

        vk->key(
            std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::released);
        QVERIFY(key_spy.wait());

        QCOMPARE(key_spy.back().at(0).toInt(), KEY_Y);
        QCOMPARE(key_spy.back().at(1).value<Wrapland::Client::Keyboard::KeyState>(),
                 Wrapland::Client::Keyboard::KeyState::Released);
        QCOMPARE(key_spy.back().at(2).toInt(), timestamp);
    }
}

}
