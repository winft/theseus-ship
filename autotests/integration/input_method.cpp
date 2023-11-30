/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/input_method_v2.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/text_input_v3.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/input_method_v2.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/text_input_v3.h>
#include <memory>

namespace KWin::detail::test
{

TEST_CASE("input method", "[input],[win]")
{
    client ti_client;
    client im_client;

    std::unique_ptr<Wrapland::Client::text_input_v3> text_input;
    std::unique_ptr<Wrapland::Client::input_method_v2> input_method;

    test::setup setup("input-method");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    ti_client = client(global_selection::seat | global_selection::text_input_manager_v3);
    im_client = client(global_selection::seat | global_selection::input_method_v2);

    text_input.reset(ti_client.interfaces.text_input_manager_v3->get_text_input(
        ti_client.interfaces.seat.get()));
    input_method.reset(im_client.interfaces.input_method_manager_v2->get_input_method(
        im_client.interfaces.seat.get()));

    QVERIFY(text_input.get());
    QVERIFY(input_method.get());

    struct {
        std::unique_ptr<Wrapland::Client::Surface> client_surface;
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
        wayland_window* window{nullptr};
    } toplevel;

    struct {
        std::unique_ptr<Wrapland::Client::Surface> client_surface;
        Wrapland::Client::input_popup_surface_v2* client_popup_surface;
        Wrapland::Server::input_method_popup_surface_v2* server_popup_surface;
        wayland_window* window{nullptr};
        QRect text_area;
        std::unique_ptr<QSignalSpy> shown_spy;
        std::unique_ptr<QSignalSpy> hidden_spy;
        std::unique_ptr<QSignalSpy> rectangle_spy;
    } popup;

    QSignalSpy input_method_spy(setup.base->server->seat(),
                                &Wrapland::Server::Seat::input_method_v2_changed);
    QVERIFY(input_method_spy.isValid());
    QVERIFY(input_method_spy.wait());
    QVERIFY(setup.base->server->seat()->get_input_method_v2());

    auto make_toplevel = [&]() {
        toplevel.client_surface = create_surface(ti_client);
        toplevel.client_toplevel = create_xdg_shell_toplevel(ti_client, toplevel.client_surface);
        toplevel.window = render_and_wait_for_shown(
            ti_client, toplevel.client_surface, QSize(1280, 1024), Qt::red);
    };

    auto enable_text_input = [&]() {
        QSignalSpy spy(setup.base->server->seat(),
                       &Wrapland::Server::Seat::text_input_v3_enabled_changed);
        QVERIFY(spy.isValid());

        popup.text_area = QRect(100, 100, 60, 30);
        text_input->enable();
        text_input->set_cursor_rectangle(popup.text_area);
        text_input->commit();

        QVERIFY(spy.wait());
        QVERIFY(spy.back().front().toBool());
    };

    auto disable_text_input = [&]() {
        QSignalSpy spy(setup.base->server->seat(),
                       &Wrapland::Server::Seat::text_input_v3_enabled_changed);
        QVERIFY(spy.isValid());

        text_input->disable();
        text_input->commit();

        QVERIFY(spy.wait());
        QVERIFY(!spy.back().front().toBool());
    };

    /// Create popup surface, check popup window is created, init spies.
    auto create_popup = [&]() {
        QSignalSpy popup_spy(setup.base->server->seat()->get_input_method_v2(),
                             &Wrapland::Server::input_method_v2::popup_surface_created);
        QVERIFY(popup_spy.isValid());

        popup.client_surface = create_surface(im_client);
        QVERIFY(popup.client_surface.get());
        popup.client_popup_surface
            = input_method->get_input_popup_surface(popup.client_surface.get());
        QVERIFY(popup_spy.wait());

        popup.server_popup_surface
            = popup_spy.front().front().value<Wrapland::Server::input_method_popup_surface_v2*>();

        popup.window = win::wayland::space_windows_find(*setup.base->space,
                                                        popup.server_popup_surface->surface());
        QVERIFY(popup.window);

        popup.shown_spy = std::make_unique<QSignalSpy>(popup.window->qobject.get(),
                                                       &win::window_qobject::windowShown);
        QVERIFY(popup.shown_spy->isValid());

        popup.hidden_spy = std::make_unique<QSignalSpy>(popup.window->qobject.get(),
                                                        &win::window_qobject::windowHidden);
        QVERIFY(popup.hidden_spy->isValid());

        popup.rectangle_spy = std::make_unique<QSignalSpy>(
            popup.client_popup_surface,
            &Wrapland::Client::input_popup_surface_v2::text_input_rectangle_changed);
        QVERIFY(popup.rectangle_spy->isValid());
    };

    auto render_popup = [&]() {
        QSignalSpy spy(popup.window->surface, &Wrapland::Server::Surface::committed);
        render(im_client, popup.client_surface, QSize(60, 30), Qt::blue);
        flush_wayland_connection(im_client);
        QVERIFY(spy.wait());
    };

    SECTION("keyboard filter")
    {
        /**
         * Create a text-input client and a keyboard grab. Verify that keyboard input is catched and
         * the filter is destroyed after release.
         */
        QSignalSpy enabled_spy(setup.base->server->seat(),
                               &Wrapland::Server::Seat::text_input_v3_enabled_changed);

        make_toplevel();

        QSignalSpy keyboard_grab_spy(setup.base->server->seat()->get_input_method_v2(),
                                     &Wrapland::Server::input_method_v2::keyboard_grabbed);
        QVERIFY(keyboard_grab_spy.isValid());

        auto keyboard_grab = input_method->grab_keyboard();
        QSignalSpy keymap_changed_spy(
            keyboard_grab, &Wrapland::Client::input_method_keyboard_grab_v2::keymap_changed);

        QVERIFY(keyboard_grab_spy.wait());
        auto server_keyboard_grab = keyboard_grab_spy.front()
                                        .front()
                                        .value<Wrapland::Server::input_method_keyboard_grab_v2*>();
        QVERIFY(server_keyboard_grab);

        // Keymap is properly set.
        QVERIFY(keymap_changed_spy.isValid());
        QVERIFY(keymap_changed_spy.wait());
        QVERIFY(keymap_changed_spy.takeFirst().at(1).value<quint32>());

        // Input method is not active, keyboard is not grabbed.
        QSignalSpy key_changed_spy(keyboard_grab,
                                   &Wrapland::Client::input_method_keyboard_grab_v2::key_changed);
        QVERIFY(key_changed_spy.isValid());
        keyboard_key_pressed(62, 1500);
        QVERIFY(!key_changed_spy.wait(500));

        // Enable text-input, trigger input method activation.
        auto const text_area = QRect(100, 100, 60, 30);
        text_input->enable();
        text_input->set_cursor_rectangle(text_area);
        text_input->commit();

        QVERIFY(enabled_spy.wait());
        QVERIFY(enabled_spy.back().front().toBool());

        // Now keyboard input is catched.
        keyboard_key_pressed(62, 1500);

        QVERIFY(key_changed_spy.wait());
        auto key_changed_payload = key_changed_spy.takeFirst();

        QCOMPARE(key_changed_payload.at(0).value<quint32>(), 62);
        QCOMPARE(key_changed_payload.at(1).value<Wrapland::Client::Keyboard::KeyState>(),
                 Wrapland::Client::Keyboard::KeyState::Pressed);
        QCOMPARE(key_changed_payload.at(2).value<quint32>(), 1500);

        keyboard_key_released(62, 1505);

        QVERIFY(key_changed_spy.wait());
        key_changed_payload = key_changed_spy.takeFirst();

        QCOMPARE(key_changed_payload.at(0).value<quint32>(), 62);
        QCOMPARE(key_changed_payload.at(1).value<Wrapland::Client::Keyboard::KeyState>(),
                 Wrapland::Client::Keyboard::KeyState::Released);
        QCOMPARE(key_changed_payload.at(2).value<quint32>(), 1505);

        // Disable text-input and destroy the keyboard grab.
        disable_text_input();

        QSignalSpy keyboard_grab_destroyed_spy(
            server_keyboard_grab,
            &Wrapland::Server::input_method_keyboard_grab_v2::resourceDestroyed);
        QVERIFY(keyboard_grab_destroyed_spy.isValid());

        delete keyboard_grab;
        QVERIFY(keyboard_grab_destroyed_spy.wait());

        // Enable text-input; the keyboard filter has been uninstalled and destroyed.
        text_input->enable();
        text_input->commit();
        QVERIFY(enabled_spy.wait());
        QVERIFY(enabled_spy.back().front().toBool());
        keyboard_key_pressed(70, 1600);
        QVERIFY(!key_changed_spy.wait(500));
    }

    SECTION("early popup")
    {
        /**
         * Create an input method popup and a text-input client afterwards. Verify that the popup is
         * drawn with acceptable geometry and the window is destroyed on release.
         */
        QSignalSpy window_added_spy(setup.base->space->qobject.get(),
                                    &space::qobject_t::wayland_window_added);
        QVERIFY(window_added_spy.isValid());

        QSignalSpy window_removed_spy(setup.base->space->qobject.get(),
                                      &space::qobject_t::wayland_window_removed);
        QVERIFY(window_removed_spy.isValid());

        QSignalSpy done_spy(input_method.get(), &Wrapland::Client::input_method_v2::done);
        QVERIFY(done_spy.isValid());

        make_toplevel();
        window_added_spy.clear();

        // Popup window is created early and before text-input is enabled.
        create_popup();
        render_popup();

        // Won't show yet.
        QVERIFY(!popup.shown_spy->wait(500));
        QVERIFY(popup.shown_spy->empty());

        // After enabling text-input shows directly.
        enable_text_input();
        QCOMPARE(window_added_spy.count(), 1);
        QCOMPARE(popup.shown_spy->count(), 1);

        // Try to render one more time. This used to crash at some point in the past.
        render_popup();

        auto signal_id = window_added_spy.back().front().value<quint32>();
        QCOMPARE(popup.window, get_wayland_window(setup.base->space->windows_map.at(signal_id)));

        QVERIFY(popup.window->isInputMethod());
        QVERIFY(!popup.text_area.intersects(popup.window->geo.frame));

        // Not yet synchronized.
        QVERIFY(!popup.client_popup_surface->text_input_rectangle().isValid());

        // The text-input state is now being synchronized with the input-method client.
        QVERIFY(popup.rectangle_spy->wait());
        QCOMPARE(done_spy.size(), 1);
        QCOMPARE(popup.text_area, popup.client_popup_surface->text_input_rectangle());

        disable_text_input();
        QVERIFY(!popup.hidden_spy->empty());

        QVERIFY(window_removed_spy.empty());
        popup.client_popup_surface->release();
        QVERIFY(window_removed_spy.wait());
        QCOMPARE(window_removed_spy.size(), 1);
    }

    SECTION("late popup")
    {
        /**
         * Create a text-input client and an input method popup afterwards. Verify that the popup is
         * drawn with acceptable geometry and the window is destroyed on release.
         */
        QSignalSpy window_added_spy(setup.base->space->qobject.get(),
                                    &space::qobject_t::wayland_window_added);
        QVERIFY(window_added_spy.isValid());

        QSignalSpy window_removed_spy(setup.base->space->qobject.get(),
                                      &space::qobject_t::wayland_window_removed);
        QVERIFY(window_removed_spy.isValid());

        QSignalSpy done_spy(input_method.get(), &Wrapland::Client::input_method_v2::done);
        QVERIFY(done_spy.isValid());

        make_toplevel();
        window_added_spy.clear();

        enable_text_input();

        // The text-input state is now being synchronized with the input-method client.
        QVERIFY(done_spy.wait());
        QCOMPARE(done_spy.size(), 1);
        done_spy.clear();

        // Popup window is created late and after text-input was enabled.
        create_popup();

        QVERIFY(popup.rectangle_spy->wait());
        QCOMPARE(done_spy.size(), 0);

        QCOMPARE(popup.text_area, popup.client_popup_surface->text_input_rectangle());

        render_popup();

        // Now shows after requests have been processed.
        TRY_REQUIRE(popup.shown_spy->size() == 1);
        QCOMPARE(window_added_spy.count(), 1);

        // Try to render one more time. This used to crash at some point in the past.
        render_popup();

        auto signal_id = window_added_spy.back().front().value<quint32>();
        QCOMPARE(popup.window, get_wayland_window(setup.base->space->windows_map.at(signal_id)));

        QVERIFY(popup.window->isInputMethod());
        QVERIFY(!popup.text_area.intersects(popup.window->geo.frame));

        disable_text_input();
        QVERIFY(!popup.hidden_spy->empty());

        QVERIFY(window_removed_spy.empty());
        popup.client_popup_surface->release();
        QVERIFY(window_removed_spy.wait());
        QCOMPARE(window_removed_spy.size(), 1);
    }
}

}
