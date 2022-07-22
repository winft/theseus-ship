/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "input/wayland/platform.h"
#include "win/space.h"
#include "win/wayland/window.h"

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

namespace KWin
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
            QSignalSpy windowDeletedSpy(window, &Toplevel::closed);
            QVERIFY(windowDeletedSpy.isValid());
            QVERIFY(Test::wait_for_destroyed(window));
            QCOMPARE(windowDeletedSpy.size(), 1);
        }
    }
    std::unique_ptr<Wrapland::Client::Surface> client_surface;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
    win::wayland::window* window{nullptr};
};

class virtual_keyboard_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_keymap();
    void test_keys();

private:
    Test::client vk_client;
    Test::client focus_client;
};

void virtual_keyboard_test::initTestCase()
{
    qRegisterMetaType<Toplevel*>();
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<Wrapland::Client::Output*>();
    qRegisterMetaType<Wrapland::Client::Keyboard::KeyState>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

std::unique_ptr<Wrapland::Client::virtual_keyboard_v1> create_virtual_keyboard(Test::client& client)
{
    return std::unique_ptr<Wrapland::Client::virtual_keyboard_v1>(
        client.interfaces.virtual_keyboard_manager_v1->create_virtual_keyboard(
            client.interfaces.seat.get()));
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

void virtual_keyboard_test::init()
{
    vk_client = Test::client(Test::global_selection::seat
                             | Test::global_selection::virtual_keyboard_manager_v1);
    focus_client = create_focus_client();
}

void virtual_keyboard_test::cleanup()
{
    // Make sure we animate.
    QTest::qWait(1000);
    QVERIFY(Test::app()->workspace->windows.empty());

    vk_client = {};
    focus_client = {};

    Test::destroy_wayland_connection();
}

/**
 * Verifies that keymaps are correctly submitted and updated.
 */
void virtual_keyboard_test::test_keymap()
{
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
    QCOMPARE(Test::app()->workspace->active_client, window.window);

    // After focus we don't yet get the current keymap as none was set yet.
    QVERIFY(!client_keymap_spy.wait(500));

    // Now we press some key, so we get the current keymap.
    uint32_t timestamp{0};
    Test::keyboard_key_pressed(KEY_Y, timestamp++);
    Test::keyboard_key_released(KEY_Y, timestamp++);
    QVERIFY(client_keymap_spy.wait());

    QSignalSpy vk_spy(
        static_cast<input::wayland::platform*>(Test::app()->input.get())->virtual_keyboard.get(),
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
    vk->key(std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::pressed);
    vk->key(std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::released);

    QVERIFY(client_keymap_spy.wait());
}

/**
 * Verifies that keys are processed.
 */
void virtual_keyboard_test::test_keys()
{
    QSignalSpy vk_spy(
        static_cast<input::wayland::platform*>(Test::app()->input.get())->virtual_keyboard.get(),
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
    QCOMPARE(Test::app()->workspace->active_client, window.window);

    // Now we press on the virtual keyboard and we should get the new new keymap.
    uint32_t timestamp{0};
    vk->key(std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::pressed);
    QVERIFY(key_spy.wait());

    QCOMPARE(key_spy.back().at(0).toInt(), KEY_Y);
    QCOMPARE(key_spy.back().at(1).value<Wrapland::Client::Keyboard::KeyState>(),
             Wrapland::Client::Keyboard::KeyState::Pressed);
    QCOMPARE(key_spy.back().at(2).toInt(), timestamp);

    vk->key(std::chrono::milliseconds(++timestamp), KEY_Y, Wrapland::Client::key_state::released);
    QVERIFY(key_spy.wait());

    QCOMPARE(key_spy.back().at(0).toInt(), KEY_Y);
    QCOMPARE(key_spy.back().at(1).value<Wrapland::Client::Keyboard::KeyState>(),
             Wrapland::Client::Keyboard::KeyState::Released);
    QCOMPARE(key_spy.back().at(2).toInt(), timestamp);
}

}

WAYLANDTEST_MAIN(KWin::virtual_keyboard_test)
#include "virtual_keyboard.moc"
