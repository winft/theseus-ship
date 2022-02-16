/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "screens.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

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

namespace KWin
{

class input_method_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_keyboard_filter();

    void test_early_popup_window();
    void test_late_popup_window();

private:
    Test::client ti_client;
    Test::client im_client;

    std::unique_ptr<Wrapland::Client::text_input_v3> text_input;
    std::unique_ptr<Wrapland::Client::input_method_v2> input_method;

    struct {
        std::unique_ptr<Wrapland::Client::Surface> client_surface;
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> client_toplevel;
        win::wayland::window* window{nullptr};
    } toplevel;

    struct {
        std::unique_ptr<Wrapland::Client::Surface> client_surface;
        Wrapland::Client::input_popup_surface_v2* client_popup_surface;
        Wrapland::Server::input_method_popup_surface_v2* server_popup_surface;
        win::wayland::window* window{nullptr};
        QRect text_area;
        std::unique_ptr<QSignalSpy> shown_spy;
        std::unique_ptr<QSignalSpy> hidden_spy;
        std::unique_ptr<QSignalSpy> rectangle_spy;
    } popup;

    void make_toplevel();
    void create_popup();
    void render_popup();
    void enable_text_input();
    void disable_text_input();
};

void input_method_test::initTestCase()
{
    qRegisterMetaType<Wrapland::Client::Keyboard::KeyState>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void input_method_test::init()
{
    ti_client = Test::client(Test::global_selection::seat
                             | Test::global_selection::text_input_manager_v3);
    im_client
        = Test::client(Test::global_selection::seat | Test::global_selection::input_method_v2);

    text_input.reset(ti_client.interfaces.text_input_manager_v3->get_text_input(
        ti_client.interfaces.seat.get()));
    input_method.reset(im_client.interfaces.input_method_manager_v2->get_input_method(
        im_client.interfaces.seat.get()));

    QVERIFY(text_input.get());
    QVERIFY(input_method.get());

    QSignalSpy input_method_spy(waylandServer()->seat(),
                                &Wrapland::Server::Seat::input_method_v2_changed);
    QVERIFY(input_method_spy.isValid());
    QVERIFY(input_method_spy.wait());
    QVERIFY(waylandServer()->seat()->get_input_method_v2());

    Test::app()->base.screens.setCurrent(0);
}

void input_method_test::cleanup()
{
    popup.client_surface.reset();

    toplevel.client_toplevel.reset();
    if (toplevel.window) {
        QSignalSpy windowDeletedSpy(toplevel.window, &Toplevel::windowClosed);
        QVERIFY(windowDeletedSpy.isValid());
        QVERIFY(Test::wait_for_destroyed(toplevel.window));
        QVERIFY(windowDeletedSpy.count() || windowDeletedSpy.wait());

        // Make sure we animate.
        QTest::qWait(1000);
    }

    toplevel.client_surface.reset();
    QVERIFY(workspace()->windows().empty());

    input_method.reset();
    text_input.reset();
    im_client = {};
    ti_client = {};

    Test::destroy_wayland_connection();
}

void input_method_test::make_toplevel()
{
    toplevel.client_surface = Test::create_surface(ti_client);
    toplevel.client_toplevel = Test::create_xdg_shell_toplevel(ti_client, toplevel.client_surface);
    toplevel.window = Test::render_and_wait_for_shown(
        ti_client, toplevel.client_surface, QSize(1280, 1024), Qt::red);
}

void input_method_test::enable_text_input()
{
    QSignalSpy spy(waylandServer()->seat(), &Wrapland::Server::Seat::text_input_v3_enabled_changed);
    QVERIFY(spy.isValid());

    popup.text_area = QRect(100, 100, 60, 30);
    text_input->enable();
    text_input->set_cursor_rectangle(popup.text_area);
    text_input->commit();

    QVERIFY(spy.wait());
    QVERIFY(spy.back().front().toBool());
}

void input_method_test::disable_text_input()
{
    QSignalSpy spy(waylandServer()->seat(), &Wrapland::Server::Seat::text_input_v3_enabled_changed);
    QVERIFY(spy.isValid());

    text_input->disable();
    text_input->commit();

    QVERIFY(spy.wait());
    QVERIFY(!spy.back().front().toBool());
}

/// Create popup surface, check popup window is created, init spies.
void input_method_test::create_popup()
{
    QSignalSpy popup_spy(waylandServer()->seat()->get_input_method_v2(),
                         &Wrapland::Server::input_method_v2::popup_surface_created);
    QVERIFY(popup_spy.isValid());

    popup.client_surface = Test::create_surface(im_client);
    QVERIFY(popup.client_surface.get());
    popup.client_popup_surface = input_method->get_input_popup_surface(popup.client_surface.get());
    QVERIFY(popup_spy.wait());

    popup.server_popup_surface
        = popup_spy.front().front().value<Wrapland::Server::input_method_popup_surface_v2*>();

    popup.window = static_cast<win::wayland::space*>(workspace())
                       ->find_window(popup.server_popup_surface->surface());
    QVERIFY(popup.window);

    popup.shown_spy
        = std::make_unique<QSignalSpy>(popup.window, &win::wayland::window::windowShown);
    QVERIFY(popup.shown_spy->isValid());

    popup.hidden_spy
        = std::make_unique<QSignalSpy>(popup.window, &win::wayland::window::windowHidden);
    QVERIFY(popup.hidden_spy->isValid());

    popup.rectangle_spy = std::make_unique<QSignalSpy>(
        popup.client_popup_surface,
        &Wrapland::Client::input_popup_surface_v2::text_input_rectangle_changed);
    QVERIFY(popup.rectangle_spy->isValid());
}

void input_method_test::render_popup()
{
    QSignalSpy spy(popup.window->surface(), &Wrapland::Server::Surface::committed);
    Test::render(im_client, popup.client_surface, QSize(60, 30), Qt::blue);
    Test::flush_wayland_connection(im_client);
    QVERIFY(spy.wait());
}

/**
 * Create an input method popup and a text-input client afterwards.
 * Verify that the popup is drawn with acceptable geometry and the window is destroyed on release.
 */
void input_method_test::test_early_popup_window()
{
    QSignalSpy window_added_spy(workspace(), &win::space::wayland_window_added);
    QVERIFY(window_added_spy.isValid());

    QSignalSpy window_removed_spy(workspace(), &win::space::wayland_window_removed);
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

    QCOMPARE(popup.window, window_added_spy.back().front().value<win::wayland::window*>());

    QVERIFY(popup.window->isInputMethod());
    QVERIFY(!popup.text_area.intersects(popup.window->frameGeometry()));

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

/**
 * Create a text-input client and an input method popup afterwards.
 * Verify that the popup is drawn with acceptable geometry and the window is destroyed on release.
 */
void input_method_test::test_late_popup_window()
{
    QSignalSpy window_added_spy(workspace(), &win::space::wayland_window_added);
    QVERIFY(window_added_spy.isValid());

    QSignalSpy window_removed_spy(workspace(), &win::space::wayland_window_removed);
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
    QVERIFY(popup.shown_spy->count() == 1 || popup.shown_spy->wait());
    QCOMPARE(window_added_spy.count(), 1);

    // Try to render one more time. This used to crash at some point in the past.
    render_popup();

    QCOMPARE(popup.window, window_added_spy.back().front().value<win::wayland::window*>());

    QVERIFY(popup.window->isInputMethod());
    QVERIFY(!popup.text_area.intersects(popup.window->frameGeometry()));

    disable_text_input();
    QVERIFY(!popup.hidden_spy->empty());

    QVERIFY(window_removed_spy.empty());
    popup.client_popup_surface->release();
    QVERIFY(window_removed_spy.wait());
    QCOMPARE(window_removed_spy.size(), 1);
}

/**
 * Create a text-input client and a keyboard grab.
 * Verify that keyboard input is catched and the filter is destroyed after release.
 */
void input_method_test::test_keyboard_filter()
{
    QSignalSpy enabled_spy(waylandServer()->seat(),
                           &Wrapland::Server::Seat::text_input_v3_enabled_changed);

    make_toplevel();

    QSignalSpy keyboard_grab_spy(waylandServer()->seat()->get_input_method_v2(),
                                 &Wrapland::Server::input_method_v2::keyboard_grabbed);
    QVERIFY(keyboard_grab_spy.isValid());

    auto keyboard_grab = input_method->grab_keyboard();
    QSignalSpy keymap_changed_spy(keyboard_grab,
                                  &Wrapland::Client::input_method_keyboard_grab_v2::keymap_changed);

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
    Test::keyboard_key_pressed(62, 1500);
    QVERIFY(!key_changed_spy.wait(500));

    // Enable text-input, trigger input method activation.
    auto const text_area = QRect(100, 100, 60, 30);
    text_input->enable();
    text_input->set_cursor_rectangle(text_area);
    text_input->commit();

    QVERIFY(enabled_spy.wait());
    QVERIFY(enabled_spy.back().front().toBool());

    // Now keyboard input is catched.
    Test::keyboard_key_pressed(62, 1500);

    QVERIFY(key_changed_spy.wait());
    auto key_changed_payload = key_changed_spy.takeFirst();

    QCOMPARE(key_changed_payload.at(0).value<quint32>(), 62);
    QCOMPARE(key_changed_payload.at(1).value<Wrapland::Client::Keyboard::KeyState>(),
             Wrapland::Client::Keyboard::KeyState::Pressed);
    QCOMPARE(key_changed_payload.at(2).value<quint32>(), 1500);

    Test::keyboard_key_released(62, 1505);

    QVERIFY(key_changed_spy.wait());
    key_changed_payload = key_changed_spy.takeFirst();

    QCOMPARE(key_changed_payload.at(0).value<quint32>(), 62);
    QCOMPARE(key_changed_payload.at(1).value<Wrapland::Client::Keyboard::KeyState>(),
             Wrapland::Client::Keyboard::KeyState::Released);
    QCOMPARE(key_changed_payload.at(2).value<quint32>(), 1505);

    // Disable text-input and destroy the keyboard grab.
    disable_text_input();

    QSignalSpy keyboard_grab_destroyed_spy(
        server_keyboard_grab, &Wrapland::Server::input_method_keyboard_grab_v2::resourceDestroyed);
    QVERIFY(keyboard_grab_destroyed_spy.isValid());

    delete keyboard_grab;
    QVERIFY(keyboard_grab_destroyed_spy.wait());

    // Enable text-input; the keyboard filter has been uninstalled and destroyed.
    text_input->enable();
    text_input->commit();
    QVERIFY(enabled_spy.wait());
    QVERIFY(enabled_spy.back().front().toBool());
    Test::keyboard_key_pressed(70, 1600);
    QVERIFY(!key_changed_spy.wait(500));
}

}

WAYLANDTEST_MAIN(KWin::input_method_test)
#include "input_method.moc"
