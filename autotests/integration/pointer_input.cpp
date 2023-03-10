/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/options.h"
#include "base/wayland/server.h"
#include "input/pointer_redirect.h"
#include "input/wayland/cursor.h"
#include "input/wayland/cursor_theme.h"
#include "render/effects.h"
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <linux/input.h>
#include <wayland-cursor.h>

namespace KWin::detail::test
{

namespace
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
};

}

TEST_CASE("pointer input", "[input]")
{
    auto hasTheme = [](const QString& name) {
        const auto path = "icons/" + name + "/index.theme";
        return !QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, path).isEmpty();
    };

    if (hasTheme("DMZ-White")) {
        qDebug() << "Using DMZ-White cursor theme.";
        qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    } else if (hasTheme("Vanilla-DMZ")) {
        // Might be Vanilla-DMZ (e.g. Arch, FreeBSD).
        qDebug() << "Using Vanilla-DMZ cursor theme.";
        qputenv("XCURSOR_THEME", QByteArrayLiteral("Vanilla-DMZ"));
    } else {
        qWarning() << "DMZ cursor theme not found. Test might fail.";
    }
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("XKB_DEFAULT_RULES", "evdev");

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("pointer-input", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(640, 512));

    auto loadReferenceThemeCursor = [&](auto const& shape) {
        if (!setup.base->server->internal_connection.shm) {
            return PlatformCursorImage();
        }

        using cursor_t = std::remove_pointer_t<decltype(cursor())>;
        auto cursorTheme = std::make_unique<input::wayland::cursor_theme<cursor_t>>(
            *cursor(), setup.base->server->internal_connection.shm);

        wl_cursor_image* cursor = cursorTheme->get(shape);
        if (!cursor) {
            return PlatformCursorImage();
        }

        wl_buffer* b = wl_cursor_image_get_buffer(cursor);
        if (!b) {
            return PlatformCursorImage();
        }

        setup.base->server->internal_connection.client->flush();
        setup.base->server->dispatch();

        auto bufferId = Wrapland::Client::Buffer::getId(b);
        auto wlResource = setup.base->server->internal_connection.server->getResource(bufferId);
        auto buffer = Wrapland::Server::Buffer::get(setup.base->server->display.get(), wlResource);
        if (!buffer) {
            return PlatformCursorImage{};
        }

        const qreal scale = setup.base->topology.max_scale;
        QImage image = buffer->shmImage()->createQImage().copy();
        image.setDevicePixelRatio(scale);

        const QPoint hotSpot(qRound(cursor->hotspot_x / scale), qRound(cursor->hotspot_y / scale));

        return PlatformCursorImage(image, hotSpot);
    };

    auto get_wayland_window_from_id = [&](uint32_t id) {
        return get_window<wayland_window>(setup.base->space->windows_map.at(id));
    };

    auto render = [](auto const& surface, QSize const& size = QSize(100, 50)) {
        test::render(surface, size, Qt::blue);
        flush_wayland_connection();
    };

    setup_wayland_connection(global_selection::seat | global_selection::xdg_decoration);
    QVERIFY(wait_for_wayland_pointer());
    auto seat = get_client().interfaces.seat.get();

    SECTION("warping updates focus")
    {
        // this test verifies that warping the pointer creates pointer enter and leave events
        using namespace Wrapland::Client;
        // create pointer and signal spy for enter and leave signals
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // currently there should not be a focused pointer surface
        QVERIFY(!setup.base->server->seat()->pointers().get_focus().surface);
        QVERIFY(!pointer->enteredSurface());

        // enter
        cursor()->set_pos(QPoint(25, 25));
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 1);
        QCOMPARE(enteredSpy.first().at(1).toPointF(), QPointF(25, 25));
        // window should have focus
        QCOMPARE(pointer->enteredSurface(), surface.get());
        // also on the server
        QCOMPARE(setup.base->server->seat()->pointers().get_focus().surface, window->surface);

        // and out again
        cursor()->set_pos(QPoint(250, 250));
        QVERIFY(leftSpy.wait());
        QCOMPARE(leftSpy.count(), 1);

        // there should not be a focused pointer surface anymore
        QVERIFY(!setup.base->server->seat()->pointers().get_focus().surface);
        QVERIFY(!pointer->enteredSurface());
    }

    SECTION("warping generates pointer moition")
    {
        // this test verifies that warping the pointer creates pointer motion events
        using namespace Wrapland::Client;
        // create pointer and signal spy for enter and motion
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy movedSpy(pointer, &Pointer::motion);
        QVERIFY(movedSpy.isValid());

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // enter
        pointer_motion_absolute(QPointF(25, 25), 1);
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.first().at(1).toPointF(), QPointF(25, 25));

        // now warp
        cursor()->set_pos(QPoint(26, 26));
        QVERIFY(movedSpy.wait());
        QCOMPARE(movedSpy.count(), 1);
        QCOMPARE(movedSpy.last().first().toPointF(), QPointF(26, 26));
    }

    SECTION("warping during filter")
    {
        // this test verifies that pointer motion is handled correctly if
        // the pointer gets warped during processing of input events
        using namespace Wrapland::Client;

        // create pointer
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy movedSpy(pointer, &Pointer::motion);
        QVERIFY(movedSpy.isValid());

        // warp cursor into expected geometry
        cursor()->set_pos(10, 10);

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        QCOMPARE(window->geo.pos(), QPoint(0, 0));
        QVERIFY(window->geo.frame.contains(cursor()->pos()));

        // is window view effect for top left screen edge loaded
        // TODO(romangg): Use OpenGl in this test and remove the expected fail once we can run tests
        // with OpenGl on CI. See https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/2871.
        REQUIRE_FALSE(setup.base->render->compositor->effects->isEffectLoaded("windowview"));
        return;

        QVERIFY(movedSpy.isEmpty());
        quint32 timestamp = 0;
        pointer_motion_absolute(QPoint(0, 0), timestamp++);

        // screen edges push back
        QCOMPARE(cursor()->pos(), QPoint(1, 1));
        QVERIFY(movedSpy.wait());
        QCOMPARE(movedSpy.count(), 2);
        QCOMPARE(movedSpy.at(0).first().toPoint(), QPoint(0, 0));
        QCOMPARE(movedSpy.at(1).first().toPoint(), QPoint(1, 1));
    }

    SECTION("update focus after screen change")
    {
        // This test verifies that a pointer enter event is generated when the cursor changes to
        // another screen due to removal of screen.
        using namespace Wrapland::Client;

        // Ensure cursor is on second screen.
        cursor()->set_pos(1500, 300);

        // Create pointer and signal spy for enter and motion.
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());

        // Create a window.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        render(surface, QSize(1280, 1024));
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);
        QVERIFY(!window->geo.frame.contains(cursor()->pos()));

        QSignalSpy screensChangedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(screensChangedSpy.isValid());

        // Now let's remove the screen containing the cursor.
        auto geometries = std::vector<QRect>({{0, 0, 1280, 1024}});
        setup.set_outputs(geometries);
        QCOMPARE(screensChangedSpy.count(), 1);
        test_outputs_geometries(geometries);

        // This should have warped the cursor.
        QCOMPARE(cursor()->pos(), QPoint(639, 511));
        QVERIFY(window->geo.frame.contains(cursor()->pos()));

        // And we should get an enter event.
        // TODO(romangg): geometry contains cursor but no enter event
        REQUIRE_FALSE(enteredSpy.size() == 1);
    }

    SECTION("modifier click unrestricted move")
    {
        // this test ensures that Alt+mouse button press triggers unrestricted move

        using namespace Wrapland::Client;

        enum class key {
            meta,
            alt,
        };

        auto key = GENERATE(key::meta, key::alt);
        auto is_left_key = GENERATE(true, false);
        auto is_capslock = GENERATE(true, false);
        auto mouse_button = GENERATE(BTN_LEFT, BTN_RIGHT, BTN_MIDDLE);

        int modifier;
        QString mod_name;
        Qt::KeyboardModifier qt_mod;

        switch (key) {
        case key::meta:
            modifier = is_left_key ? KEY_LEFTMETA : KEY_RIGHTMETA;
            mod_name = "Meta";
            qt_mod = Qt::MetaModifier;
            break;
        case key::alt:
            modifier = is_left_key ? KEY_LEFTALT : KEY_RIGHTALT;
            mod_name = "Alt";
            qt_mod = Qt::AltModifier;
            break;
        default:
            REQUIRE(false);
        };

        // create pointer and signal spy for button events
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy buttonSpy(pointer, &Pointer::buttonStateChanged);
        QVERIFY(buttonSpy.isValid());

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", mod_name);
        group.writeEntry("CommandAll1", "Move");
        group.writeEntry("CommandAll2", "Move");
        group.writeEntry("CommandAll3", "Move");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->commandAllModifier(), qt_mod);
        QCOMPARE(setup.base->options->qobject->commandAll1(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll2(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll3(),
                 base::options_qobject::MouseUnrestrictedMove);

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // move cursor on window
        cursor()->set_pos(window->geo.frame.center());

        // simulate modifier+click
        quint32 timestamp = 1;

        if (is_capslock) {
            keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        }

        keyboard_key_pressed(modifier, timestamp++);
        QVERIFY(!win::is_move(window));
        pointer_button_pressed(mouse_button, timestamp++);
        QVERIFY(win::is_move(window));

        // release modifier should not change it
        keyboard_key_released(modifier, timestamp++);
        QVERIFY(win::is_move(window));

        // but releasing the key should end move/resize
        pointer_button_released(mouse_button, timestamp++);
        QVERIFY(!win::is_move(window));

        if (is_capslock) {
            keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        }

        // all of that should not have triggered button events on the surface
        QCOMPARE(buttonSpy.count(), 0);

        // also waiting shouldn't give us the event
        QVERIFY(!buttonSpy.wait(100));
    }

    SECTION("modifier click unrestricted move global shortcuts disabled")
    {
        // this test ensures that Alt+mouse button press triggers unrestricted move
        using namespace Wrapland::Client;
        // create pointer and signal spy for button events
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy buttonSpy(pointer, &Pointer::buttonStateChanged);
        QVERIFY(buttonSpy.isValid());

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAll1", "Move");
        group.writeEntry("CommandAll2", "Move");
        group.writeEntry("CommandAll3", "Move");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->commandAllModifier(), Qt::MetaModifier);
        QCOMPARE(setup.base->options->qobject->commandAll1(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll2(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll3(),
                 base::options_qobject::MouseUnrestrictedMove);

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // disable global shortcuts
        QVERIFY(!setup.base->space->global_shortcuts_disabled);
        win::set_global_shortcuts_disabled(*setup.base->space, true);
        QVERIFY(setup.base->space->global_shortcuts_disabled);

        // move cursor on window
        cursor()->set_pos(window->geo.frame.center());

        // simulate modifier+click
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        QVERIFY(!win::is_move(window));
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(!win::is_move(window));
        // release modifier should not change it
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        QVERIFY(!win::is_move(window));
        pointer_button_released(BTN_LEFT, timestamp++);

        win::set_global_shortcuts_disabled(*setup.base->space, false);
    }

    SECTION("scroll opacity")
    {
        // this test verifies that mod+wheel performs a window operation and does not
        // pass the wheel to the window

        using namespace Wrapland::Client;

        enum class key {
            meta,
            alt,
        };

        auto key = GENERATE(key::meta, key::alt);
        auto is_left_key = GENERATE(true, false);
        auto is_capslock = GENERATE(true, false);

        int modifier;
        QString mod_name;

        switch (key) {
        case key::meta:
            modifier = is_left_key ? KEY_LEFTMETA : KEY_RIGHTMETA;
            mod_name = "Meta";
            break;
        case key::alt:
            modifier = is_left_key ? KEY_LEFTALT : KEY_RIGHTALT;
            mod_name = "Alt";
            break;
        default:
            REQUIRE(false);
        };

        // create pointer and signal spy for button events
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy axisSpy(pointer, &Pointer::axisChanged);
        QVERIFY(axisSpy.isValid());

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", mod_name);
        group.writeEntry("CommandAllWheel", "change opacity");
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);
        // set the opacity to 0.5
        window->setOpacity(0.5);
        QCOMPARE(window->opacity(), 0.5);

        // move cursor on window
        cursor()->set_pos(window->geo.frame.center());

        // simulate modifier+wheel
        quint32 timestamp = 1;

        if (is_capslock) {
            keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        }

        keyboard_key_pressed(modifier, timestamp++);
        pointer_axis_vertical(-5, timestamp++, 0);
        QCOMPARE(window->opacity(), 0.6);
        pointer_axis_vertical(5, timestamp++, 0);
        QCOMPARE(window->opacity(), 0.5);
        keyboard_key_released(modifier, timestamp++);

        if (is_capslock) {
            keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        }

        // axis should have been filtered out
        QCOMPARE(axisSpy.count(), 0);
        QVERIFY(!axisSpy.wait(100));
    }

    SECTION("scroll opacity global shortcuts disabled")
    {
        // this test verifies that mod+wheel performs a window operation and does not
        // pass the wheel to the window
        using namespace Wrapland::Client;

        // create pointer and signal spy for button events
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy axisSpy(pointer, &Pointer::axisChanged);
        QVERIFY(axisSpy.isValid());

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAllWheel", "change opacity");
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // create a window
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);
        // set the opacity to 0.5
        window->setOpacity(0.5);
        QCOMPARE(window->opacity(), 0.5);

        // move cursor on window
        cursor()->set_pos(window->geo.frame.center());

        // disable global shortcuts
        QVERIFY(!setup.base->space->global_shortcuts_disabled);
        win::set_global_shortcuts_disabled(*setup.base->space, true);
        QVERIFY(setup.base->space->global_shortcuts_disabled);

        // simulate modifier+wheel
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        pointer_axis_vertical(-5, timestamp++, 0);
        QCOMPARE(window->opacity(), 0.5);
        pointer_axis_vertical(5, timestamp++, 0);
        QCOMPARE(window->opacity(), 0.5);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);

        win::set_global_shortcuts_disabled(*setup.base->space, false);
    }

    SECTION("scroll action")
    {
        // this test verifies that scroll on inactive window performs a mouse action
        using namespace Wrapland::Client;

        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy axisSpy(pointer, &Pointer::axisChanged);
        QVERIFY(axisSpy.isValid());

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandWindowWheel", "activate and scroll");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        // create two windows
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface1 = create_surface();
        QVERIFY(surface1);
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(shellSurface1);
        render(surface1);
        QVERIFY(clientAddedSpy.wait());

        auto window1 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window1);
        auto surface2 = create_surface();
        QVERIFY(surface2);
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(shellSurface2);
        render(surface2);
        QVERIFY(clientAddedSpy.wait());

        auto window2 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window2);
        QVERIFY(window1 != window2);

        // move cursor to the inactive window
        cursor()->set_pos(window1->geo.frame.center());

        quint32 timestamp = 1;
        QVERIFY(!window1->control->active);
        pointer_axis_vertical(5, timestamp++, 0);
        QVERIFY(window1->control->active);

        // but also the wheel event should be passed to the window
        QVERIFY(axisSpy.wait());

        // we need to wait a little bit, otherwise the test crashes in effectshandler, needs fixing
        QTest::qWait(100);
    }

    SECTION("focus follows mouse")
    {
        using namespace Wrapland::Client;

        // need to create a pointer, otherwise it doesn't accept focus
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());

        // move cursor out of the way of first window to be created
        cursor()->set_pos(900, 900);

        // first modify the config for this run
        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("AutoRaise", true);
        group.writeEntry("AutoRaiseInterval", 20);
        group.writeEntry("DelayFocusInterval", 200);
        group.writeEntry("FocusPolicy", "FocusFollowsMouse");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        // verify the settings
        QCOMPARE(setup.base->options->qobject->focusPolicy(),
                 base::options_qobject::FocusFollowsMouse);
        QVERIFY(setup.base->options->qobject->isAutoRaise());
        QCOMPARE(setup.base->options->qobject->autoRaiseInterval(), 20);
        QCOMPARE(setup.base->options->qobject->delayFocusInterval(), 200);

        // create two windows
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface1 = create_surface();
        QVERIFY(surface1);
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(shellSurface1);
        render(surface1, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window1 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window1);
        auto surface2 = create_surface();
        QVERIFY(surface2);
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(shellSurface2);
        render(surface2, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window2 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window2);
        QVERIFY(window1 != window2);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window2);
        // geometry of the two windows should be overlapping
        QVERIFY(window1->geo.frame.intersects(window2->geo.frame));

        // signal spies for active window changed and stacking order changed
        QSignalSpy activeWindowChangedSpy(setup.base->space->qobject.get(),
                                          &win::space::qobject_t::clientActivated);
        QVERIFY(activeWindowChangedSpy.isValid());
        QSignalSpy stackingOrderChangedSpy(setup.base->space->stacking.order.qobject.get(),
                                           &win::stacking_order_qobject::changed);
        QVERIFY(stackingOrderChangedSpy.isValid());

        QVERIFY(!window1->control->active);
        QVERIFY(window2->control->active);

        // move on top of first window
        QVERIFY(window1->geo.frame.contains(10, 10));
        QVERIFY(!window2->geo.frame.contains(10, 10));
        cursor()->set_pos(10, 10);
        QVERIFY(stackingOrderChangedSpy.wait());
        QCOMPARE(stackingOrderChangedSpy.count(), 1);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window1);
        QTRY_VERIFY(window1->control->active);

        // move on second window, but move away before active window change delay hits
        cursor()->set_pos(810, 810);
        QVERIFY(stackingOrderChangedSpy.wait());
        QCOMPARE(stackingOrderChangedSpy.count(), 2);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window2);
        cursor()->set_pos(10, 10);
        QVERIFY(!activeWindowChangedSpy.wait(200));
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window1);

        // as we moved back on window 1 that should been raised in the mean time
        QCOMPARE(stackingOrderChangedSpy.count(), 3);

        // quickly move on window 2 and back on window 1 should not raise window 2
        cursor()->set_pos(810, 810);
        cursor()->set_pos(10, 10);
        QVERIFY(!stackingOrderChangedSpy.wait(200));
    }

    SECTION("mouse action inactive window")
    {
        // This test performs the mouse button window action on an inactive window it should
        // activate the window and raise it.
        using namespace Wrapland::Client;

        auto button = GENERATE(BTN_LEFT, BTN_MIDDLE, BTN_RIGHT);

        // First modify the config for this run - disable FocusFollowsMouse.
        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("FocusPolicy", "ClickToFocus");
        group.sync();
        group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandWindow1", "Activate, raise and pass click");
        group.writeEntry("CommandWindow2", "Activate, raise and pass click");
        group.writeEntry("CommandWindow3", "Activate, raise and pass click");
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // Create two windows.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        auto surface1 = create_surface();
        QVERIFY(surface1);
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(shellSurface1);

        render(surface1, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window1 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window1);

        auto surface2 = create_surface();
        QVERIFY(surface2);
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(shellSurface2);

        render(surface2, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window2 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window2);
        QVERIFY(window1 != window2);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window2);

        // Geometry of the two windows should be overlapping.
        QVERIFY(window1->geo.frame.intersects(window2->geo.frame));

        // Signal spies for active window changed and stacking order changed.
        QSignalSpy activeWindowChangedSpy(setup.base->space->qobject.get(),
                                          &win::space::qobject_t::clientActivated);
        QVERIFY(activeWindowChangedSpy.isValid());
        QSignalSpy stackingOrderChangedSpy(setup.base->space->stacking.order.qobject.get(),
                                           &win::stacking_order_qobject::changed);
        QVERIFY(stackingOrderChangedSpy.isValid());

        QVERIFY(!window1->control->active);
        QVERIFY(window2->control->active);

        // Move on top of first window.
        QVERIFY(window1->geo.frame.contains(10, 10));
        QVERIFY(!window2->geo.frame.contains(10, 10));
        cursor()->set_pos(10, 10);

        // No focus follows mouse.
        QVERIFY(!stackingOrderChangedSpy.wait(200));
        QVERIFY(stackingOrderChangedSpy.isEmpty());
        QVERIFY(activeWindowChangedSpy.isEmpty());
        QVERIFY(window2->control->active);

        // And click.
        quint32 timestamp = 1;
        pointer_button_pressed(button, timestamp++);

        // Should raise window1 and activate it.
        QCOMPARE(stackingOrderChangedSpy.count(), 1);
        QVERIFY(!activeWindowChangedSpy.isEmpty());
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window1);
        QVERIFY(window1->control->active);
        QVERIFY(!window2->control->active);

        // Release again.
        pointer_button_released(button, timestamp++);
    }

    SECTION("mouse action active window")
    {
        // This test verifies the mouse action performed on an active window for all buttons it
        // should trigger a window raise depending on the click raise option.
        using namespace Wrapland::Client;

        auto click_raise = GENERATE(true, false);
        auto button = GENERATE(range(BTN_LEFT, BTN_JOYSTICK));

        // Create a button spy - all clicks should be passed through.
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy buttonSpy(pointer, &Pointer::buttonStateChanged);
        QVERIFY(buttonSpy.isValid());

        // Adjust config for this run.
        auto group = setup.base->config.main->group("Windows");
        group.writeEntry("ClickRaise", click_raise);
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->isClickRaise(), click_raise);

        // Create two windows.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        auto surface1 = create_surface();
        QVERIFY(surface1);
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(shellSurface1);
        render(surface1, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window1 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window1);
        QSignalSpy window1DestroyedSpy(window1->qobject.get(), &QObject::destroyed);
        QVERIFY(window1DestroyedSpy.isValid());

        auto surface2 = create_surface();
        QVERIFY(surface2);
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(shellSurface2);
        render(surface2, QSize(800, 800));
        QVERIFY(clientAddedSpy.wait());

        auto window2 = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window2);
        QVERIFY(window1 != window2);

        QSignalSpy window2DestroyedSpy(window2->qobject.get(), &QObject::destroyed);
        QVERIFY(window2DestroyedSpy.isValid());
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window2);

        // Geometry of the two windows should be overlapping.
        QVERIFY(window1->geo.frame.intersects(window2->geo.frame));

        // lower the currently active window
        win::lower_window(*setup.base->space, window2);
        QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                 window1);

        // Signal spy for stacking order spy.
        QSignalSpy stackingOrderChangedSpy(setup.base->space->stacking.order.qobject.get(),
                                           &win::stacking_order_qobject::changed);
        QVERIFY(stackingOrderChangedSpy.isValid());

        // Move on top of second window.
        QVERIFY(!window1->geo.frame.contains(900, 900));
        QVERIFY(window2->geo.frame.contains(900, 900));
        cursor()->set_pos(900, 900);

        // And click.
        quint32 timestamp = 1;
        pointer_button_pressed(button, timestamp++);
        QVERIFY(buttonSpy.wait());

        if (click_raise) {
            QCOMPARE(stackingOrderChangedSpy.count(), 1);
            QTRY_COMPARE_WITH_TIMEOUT(
                get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                window2,
                200);
        } else {
            QCOMPARE(stackingOrderChangedSpy.count(), 0);
            QVERIFY(!stackingOrderChangedSpy.wait(100));
            QCOMPARE(get_wayland_window(win::top_client_on_desktop(*setup.base->space, 1, nullptr)),
                     window1);
        }

        // Release again.
        pointer_button_released(button, timestamp++);

        surface1.reset();
        QVERIFY(window1DestroyedSpy.wait());
        surface2.reset();
        QVERIFY(window2DestroyedSpy.wait());
    }

    SECTION("cursor image")
    {
        // This test verifies that the pointer image gets updated correctly from the client provided
        // data.
        using namespace Wrapland::Client;

        // We need a pointer to get the enter event.
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());

        // Move cursor somewhere the new window won't open.
        cursor()->set_pos(800, 800);
        auto& p = setup.base->space->input->pointer;

        // At the moment it should be the fallback cursor.
        auto const fallback_cursor = cursor()->image();
        QVERIFY(!fallback_cursor.isNull());

        // Create a window.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // Move the cursor to center of window. This should first set a null pointer. So we still
        // show the old cursor.
        cursor()->set_pos(window->geo.frame.center());
        QCOMPARE(get_wayland_window(p->focus.window), window);
        QCOMPARE(cursor()->image(), fallback_cursor);
        QVERIFY(enteredSpy.wait());

        // Create a cursor on the pointer.
        auto cursorSurface = create_surface();
        QVERIFY(cursorSurface);
        QSignalSpy cursorRenderedSpy(cursorSurface.get(), &Surface::frameRendered);
        QVERIFY(cursorRenderedSpy.isValid());

        auto red = QImage(QSize(10, 10), QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);

        cursorSurface->attachBuffer(get_client().interfaces.shm->createBuffer(red));
        cursorSurface->damage(QRect(0, 0, 10, 10));
        cursorSurface->commit();

        pointer->setCursor(cursorSurface.get(), QPoint(5, 5));
        QVERIFY(cursorRenderedSpy.wait());
        QCOMPARE(cursor()->image(), red);
        QCOMPARE(cursor()->hotspot(), QPoint(5, 5));

        // Change hotspot.
        pointer->setCursor(cursorSurface.get(), QPoint(6, 6));
        flush_wayland_connection();
        QTRY_COMPARE(cursor()->hotspot(), QPoint(6, 6));
        QCOMPARE(cursor()->image(), red);

        // Change the buffer.
        auto blue = QImage(QSize(10, 10), QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);

        auto b = get_client().interfaces.shm->createBuffer(blue);
        cursorSurface->attachBuffer(b);
        cursorSurface->damage(QRect(0, 0, 10, 10));
        cursorSurface->commit();

        QVERIFY(cursorRenderedSpy.wait());
        QTRY_COMPARE(cursor()->image(), blue);
        QCOMPARE(cursor()->hotspot(), QPoint(6, 6));

        // Scaled cursor
        auto blueScaled = QImage(QSize(20, 20), QImage::Format_ARGB32_Premultiplied);
        blueScaled.setDevicePixelRatio(2);
        blueScaled.fill(Qt::blue);

        auto bs = get_client().interfaces.shm->createBuffer(blueScaled);
        cursorSurface->attachBuffer(bs);
        cursorSurface->setScale(2);
        cursorSurface->damage(QRect(0, 0, 20, 20));
        cursorSurface->commit();

        QVERIFY(cursorRenderedSpy.wait());
        QTRY_COMPARE(cursor()->image(), blueScaled);

        // Surface-local (so not changed)
        QCOMPARE(cursor()->hotspot(), QPoint(6, 6));

        // Hide the cursor.
        pointer->setCursor(nullptr);

        flush_wayland_connection();
        QTRY_VERIFY(cursor()->image().isNull());

        // Move cursor somewhere else, should reset to fallback cursor.
        cursor()->set_pos(window->geo.frame.bottomLeft() + QPoint(20, 20));
        QVERIFY(!p->focus.window);
        QVERIFY(!cursor()->image().isNull());
        QCOMPARE(cursor()->image(), fallback_cursor);
    }

    SECTION("effect override cursor image")
    {
        // This test verifies the effect cursor override handling.
        using namespace Wrapland::Client;

        // We need a pointer to get the enter event and set a cursor.
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());

        // Move cursor somewhere the new window won't open.
        cursor()->set_pos(800, 800);

        // Mere we should have the fallback cursor.
        auto const fallback_cursor = cursor()->image();
        QVERIFY(!fallback_cursor.isNull());

        // Now let's create a window.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        render(surface);
        QVERIFY(clientAddedSpy.wait());
        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // And move cursor to the window.
        QVERIFY(!window->geo.frame.contains(QPoint(800, 800)));
        cursor()->set_pos(window->geo.frame.center());
        QVERIFY(enteredSpy.wait());

        // Cursor image should still be fallback.
        QCOMPARE(cursor()->image(), fallback_cursor);

        // Now create an effect and set an override cursor.
        std::unique_ptr<HelperEffect> effect(new HelperEffect);
        effects->startMouseInterception(effect.get(), Qt::SizeAllCursor);

        const QImage sizeAll = cursor()->image();
        QVERIFY(!sizeAll.isNull());
        QVERIFY(sizeAll != fallback_cursor);
        QVERIFY(leftSpy.wait());

        // Let's change to arrow cursor, this should be our fallback.
        effects->defineCursor(Qt::ArrowCursor);
        QCOMPARE(cursor()->image(), fallback_cursor);

        // Back to size all.
        effects->defineCursor(Qt::SizeAllCursor);
        QCOMPARE(cursor()->image(), sizeAll);

        // Move cursor outside the window area.
        cursor()->set_pos(800, 800);

        // And end the override, which should switch to fallback.
        effects->stopMouseInterception(effect.get());
        QCOMPARE(cursor()->image(), fallback_cursor);

        // Start mouse interception again.
        effects->startMouseInterception(effect.get(), Qt::SizeAllCursor);
        QCOMPARE(cursor()->image(), sizeAll);

        // Move cursor to area of window.
        cursor()->set_pos(window->geo.frame.center());

        // This should not result in an enter event.
        QVERIFY(!enteredSpy.wait(100));

        // After ending the interception we should get an enter event.
        effects->stopMouseInterception(effect.get());
        QVERIFY(enteredSpy.wait());
        QVERIFY(cursor()->image().isNull());
    }

    SECTION("popup")
    {
        // this test validates the basic popup behavior
        // a button press outside the window should dismiss the popup

        // first create a parent surface
        using namespace Wrapland::Client;
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());
        QSignalSpy buttonStateChangedSpy(pointer, &Pointer::buttonStateChanged);
        QVERIFY(buttonStateChangedSpy.isValid());
        QSignalSpy motionSpy(pointer, &Pointer::motion);
        QVERIFY(motionSpy.isValid());

        cursor()->set_pos(800, 800);

        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);
        QCOMPARE(window->transient->input_grab, false);
        // move pointer into window
        QVERIFY(!window->geo.frame.contains(QPoint(800, 800)));
        cursor()->set_pos(window->geo.frame.center());
        QVERIFY(enteredSpy.wait());

        // click inside window to create serial
        quint32 timestamp = 0;
        pointer_button_pressed(BTN_LEFT, timestamp++);
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(buttonStateChangedSpy.wait());

        // Now create the popup surface.
        //
        // ---------------------
        // |      parent       |
        // |       ---------------------
        // |       |                   |
        // |       |                   |
        // |       |       popup       |
        // --------|                   |
        //         |                   |
        //         ---------------------
        //
        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(100, 50);
        pos_data.anchor.rect = QRect(0, 0, 80, 20);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
        pos_data.gravity = pos_data.anchor.edge;

        auto popupSurface = create_surface();
        QVERIFY(popupSurface);
        auto popupShellSurface = create_xdg_shell_popup(popupSurface, shellSurface, pos_data);
        QVERIFY(popupShellSurface);
        QSignalSpy popupDoneSpy(popupShellSurface.get(), &XdgShellPopup::popupDone);
        QVERIFY(popupDoneSpy.isValid());
        popupShellSurface->requestGrab(seat, 0); // FIXME: Serial.
        render(popupSurface, pos_data.size);
        QVERIFY(clientAddedSpy.wait());

        auto popupClient
            = get_wayland_window_from_id(clientAddedSpy.last().first().value<quint32>());
        QVERIFY(popupClient);
        QVERIFY(popupClient != window);
        QCOMPARE(window, get_wayland_window(setup.base->space->stacking.active));
        QCOMPARE(popupClient->transient->lead(), window);
        QCOMPARE(popupClient->geo.pos(), window->geo.pos() + QPoint(80, 20));
        QCOMPARE(popupClient->transient->input_grab, true);
        QVERIFY(popupClient->mapped);

        // Let's move the pointer into the center of the window.
        cursor()->set_pos(popupClient->geo.frame.center());
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 2);
        QCOMPARE(leftSpy.count(), 1);
        QCOMPARE(pointer->enteredSurface(), popupSurface.get());

        // Let's move the pointer outside of the popup window but inside the parent window.
        // This should not really change anything, client gets an enter/leave event combo.
        cursor()->set_pos(QPoint(10, 10));
        QVERIFY(window->geo.frame.contains(cursor()->pos()));
        QVERIFY(!popupClient->geo.frame.contains(cursor()->pos()));
        QVERIFY(enteredSpy.wait());
        QCOMPARE(enteredSpy.count(), 3);
        QCOMPARE(leftSpy.count(), 2);
        QVERIFY(popupDoneSpy.isEmpty());

        // Now click, should not trigger popupDone but receive button events client-side..
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(buttonStateChangedSpy.wait());
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(buttonStateChangedSpy.wait());
        QVERIFY(popupDoneSpy.isEmpty());

        // Let's move the pointer outside of both windows.
        // This should not really change anything, client gets a leave event.
        cursor()->set_pos(popupClient->geo.frame.bottomRight() + QPoint(2, 2));
        QVERIFY(!window->geo.frame.contains(cursor()->pos()));
        QVERIFY(!popupClient->geo.frame.contains(cursor()->pos()));
        QVERIFY(leftSpy.wait());
        QCOMPARE(leftSpy.count(), 3);
        QVERIFY(popupDoneSpy.isEmpty());

        // Now click, should trigger popupDone.
        buttonStateChangedSpy.clear();
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(popupDoneSpy.wait());
        QVERIFY(buttonStateChangedSpy.empty());
        pointer_button_released(BTN_LEFT, timestamp++);
    }

    SECTION("deco cancels popup")
    {
        // this test verifies that clicking the window decoration of parent window
        // cancels the popup

        // first create a parent surface
        using namespace Wrapland::Client;
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());
        QSignalSpy buttonStateChangedSpy(pointer, &Pointer::buttonStateChanged);
        QVERIFY(buttonStateChangedSpy.isValid());
        QSignalSpy motionSpy(pointer, &Pointer::motion);
        QVERIFY(motionSpy.isValid());

        cursor()->set_pos(800, 800);
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        QVERIFY(shellSurface);

        auto deco = get_client().interfaces.xdg_decoration->getToplevelDecoration(
            shellSurface.get(), shellSurface.get());
        QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
        QVERIFY(decoSpy.isValid());
        deco->setMode(XdgDecoration::Mode::ServerSide);
        QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
        init_xdg_shell_toplevel(surface, shellSurface);
        QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);
        QCOMPARE(window->transient->input_grab, false);
        QVERIFY(win::decoration(window));

        // move pointer into window
        QVERIFY(!window->geo.frame.contains(QPoint(800, 800)));
        cursor()->set_pos(window->geo.frame.center());
        QVERIFY(enteredSpy.wait());
        // click inside window to create serial
        quint32 timestamp = 0;
        pointer_button_pressed(BTN_LEFT, timestamp++);
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(buttonStateChangedSpy.wait());

        // now create the popup surface
        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(100, 50);
        pos_data.anchor.rect = QRect(0, 0, 80, 20);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
        pos_data.gravity = pos_data.anchor.edge;

        auto popupSurface = create_surface();
        QVERIFY(popupSurface);
        auto popupShellSurface = create_xdg_shell_popup(popupSurface, shellSurface, pos_data);
        QVERIFY(popupShellSurface);
        QSignalSpy popupDoneSpy(popupShellSurface.get(), &XdgShellPopup::popupDone);
        QVERIFY(popupDoneSpy.isValid());
        popupShellSurface->requestGrab(seat, 0); // FIXME: Serial.
        render(popupSurface, pos_data.size);
        QVERIFY(clientAddedSpy.wait());

        auto popupClient
            = get_wayland_window_from_id(clientAddedSpy.last().first().value<quint32>());
        QVERIFY(popupClient);
        QVERIFY(popupClient != window);
        QCOMPARE(window, get_wayland_window(setup.base->space->stacking.active));
        QCOMPARE(popupClient->transient->lead(), window);
        QCOMPARE(popupClient->geo.pos(),
                 win::frame_to_client_pos(window, window->geo.pos()) + QPoint(80, 20));
        QCOMPARE(popupClient->transient->input_grab, true);

        // let's move the pointer into the center of the deco
        cursor()->set_pos(window->geo.frame.center().x(),
                          window->geo.pos().y()
                              + (window->geo.size().height()
                                 - win::frame_to_client_size(window, window->geo.size()).height())
                                  / 2);

        pointer_button_pressed(BTN_RIGHT, timestamp++);
        QVERIFY(popupDoneSpy.wait());
        pointer_button_released(BTN_RIGHT, timestamp++);
    }

    SECTION("window under cursor while button pressed")
    {
        // this test verifies that opening a window underneath the mouse cursor does not
        // trigger a leave event if a button is pressed
        // see BUG: 372876

        // first create a parent surface
        using namespace Wrapland::Client;
        auto pointer = seat->createPointer(seat);
        QVERIFY(pointer);
        QVERIFY(pointer->isValid());
        QSignalSpy enteredSpy(pointer, &Pointer::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(pointer, &Pointer::left);
        QVERIFY(leftSpy.isValid());

        cursor()->set_pos(800, 800);
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface);
        QVERIFY(clientAddedSpy.wait());

        auto window = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(window);

        // move cursor over window
        QVERIFY(!window->geo.frame.contains(QPoint(800, 800)));
        cursor()->set_pos(window->geo.frame.center());
        QVERIFY(enteredSpy.wait());
        // click inside window
        quint32 timestamp = 0;
        pointer_button_pressed(BTN_LEFT, timestamp++);

        // now create a second window as transient
        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(99, 49);
        pos_data.anchor.rect = QRect(0, 0, 1, 1);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
        pos_data.gravity = pos_data.anchor.edge;

        auto popupSurface = create_surface();
        QVERIFY(popupSurface);
        auto popupShellSurface = create_xdg_shell_popup(popupSurface, shellSurface, pos_data);
        QVERIFY(popupShellSurface);
        render(popupSurface, pos_data.size);
        QVERIFY(clientAddedSpy.wait());
        auto popupClient
            = get_wayland_window_from_id(clientAddedSpy.last().first().value<quint32>());
        QVERIFY(popupClient);
        QVERIFY(popupClient != window);
        QVERIFY(window->geo.frame.contains(cursor()->pos()));
        QVERIFY(popupClient->geo.frame.contains(cursor()->pos()));
        QVERIFY(!leftSpy.wait());

        pointer_button_released(BTN_LEFT, timestamp++);
        // now that the button is no longer pressed we should get the leave event
        QVERIFY(leftSpy.wait());
        QCOMPARE(leftSpy.count(), 1);
        QCOMPARE(enteredSpy.count(), 2);
    }

    SECTION("confine to screen geometry")
    {
        // this test verifies that pointer belongs to at least one screen
        // after moving it to off-screen area

        // screen layout:
        //
        // +----------+----------+---------+
        // |   left   |    top   |  right  |
        // +----------+----------+---------+
        //            |  bottom  |
        //            +----------+
        //

        enum class screen {
            left,
            top,
            right,
            bottom,
        };

        struct data {
            screen start_screen;
            QPoint target;
            QPoint expected;
        };

        auto test_data = GENERATE(data{screen::left, {-100, -100}, {}},
                                  data{screen::left, {640, -100}, {640, 0}},
                                  data{screen::left, {1380, -100}, {1380, 0}},
                                  data{screen::left, {1380, 512}, {1380, 512}},
                                  data{screen::left, {1380, 1124}, {1380, 1124}},
                                  data{screen::left, {640, 1124}, {640, 1023}},
                                  data{screen::left, {-100, 1124}, {0, 1023}},
                                  data{screen::left, {-100, 512}, {0, 512}},
                                  data{screen::top, {1180, -100}, {1180, 0}},
                                  data{screen::top, {1920, -100}, {1920, 0}},
                                  data{screen::top, {2660, -100}, {2660, 0}},
                                  data{screen::top, {2660, 512}, {2660, 512}},
                                  data{screen::top, {2660, 1124}, {2559, 1023}},
                                  data{screen::top, {1920, 1124}, {1920, 1124}},
                                  data{screen::top, {1180, 1124}, {1280, 1023}},
                                  data{screen::top, {1180, 512}, {1180, 512}},
                                  data{screen::right, {2460, -100}, {2460, 0}},
                                  data{screen::right, {3200, -100}, {3200, 0}},
                                  data{screen::right, {3940, -100}, {3839, 0}},
                                  data{screen::right, {3940, 512}, {3839, 512}},
                                  data{screen::right, {3940, 1124}, {3839, 1023}},
                                  data{screen::right, {3200, 1124}, {3200, 1023}},
                                  data{screen::right, {2460, 1124}, {2460, 1124}},
                                  data{screen::right, {2460, 512}, {2460, 512}},
                                  data{screen::bottom, {1180, 924}, {1180, 924}},
                                  data{screen::bottom, {1920, 924}, {1920, 924}},
                                  data{screen::bottom, {2660, 924}, {2660, 924}},
                                  data{screen::bottom, {2660, 1536}, {2559, 1536}},
                                  data{screen::bottom, {2660, 2148}, {2559, 2047}},
                                  data{screen::bottom, {1920, 2148}, {1920, 2047}},
                                  data{screen::bottom, {1180, 2148}, {1280, 2047}},
                                  data{screen::bottom, {1180, 1536}, {1280, 1536}});

        QPoint start;

        switch (test_data.start_screen) {
        case screen::left:
            start = {640, 512};
            break;
        case screen::top:
            start = {1920, 512};
            break;
        case screen::right:
            start = {3200, 512};
            break;
        case screen::bottom:
            start = {1920, 1536};
            break;
        default:
            REQUIRE(false);
        };

        // unload the window view effect because it pushes back
        // pointer if it's at (0, 0)
        setup.base->render->compositor->effects->unloadEffect(QStringLiteral("windowview"));

        // setup screen layout
        auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024},
                                                   {1280, 0, 1280, 1024},
                                                   {2560, 0, 1280, 1024},
                                                   {1280, 1024, 1280, 1024}};
        setup.set_outputs(geometries);
        test_outputs_geometries(geometries);

        // move pointer to initial position
        cursor()->set_pos(start);
        QCOMPARE(cursor()->pos(), start);

        // perform movement
        pointer_motion_absolute(test_data.target, 1);
        QCOMPARE(cursor()->pos(), test_data.expected);
    }

    SECTION("resize cursor")
    {
        // this test verifies that the cursor has correct shape during resize operation

        struct data {
            Qt::Edges edges;
            input::cursor_shape cursor_shape;
        };

        auto test_data
            = GENERATE(data{Qt::TopEdge | Qt::LeftEdge, input::extended_cursor::SizeNorthWest},
                       data{Qt::TopEdge, input::extended_cursor::SizeNorth},
                       data{Qt::TopEdge | Qt::RightEdge, input::extended_cursor::SizeNorthEast},
                       data{Qt::RightEdge, input::extended_cursor::SizeEast},
                       data{Qt::BottomEdge | Qt::RightEdge, input::extended_cursor::SizeSouthEast},
                       data{Qt::BottomEdge, input::extended_cursor::SizeSouth},
                       data{Qt::BottomEdge | Qt::LeftEdge, input::extended_cursor::SizeSouthWest},
                       data{Qt::LeftEdge, input::extended_cursor::SizeWest});

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAll3", "Resize");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->commandAllModifier(), Qt::MetaModifier);
        QCOMPARE(setup.base->options->qobject->commandAll3(),
                 base::options_qobject::MouseUnrestrictedResize);

        // create a test client
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // move the cursor to the test position
        QPoint cursorPos;

        if (test_data.edges & Qt::LeftEdge) {
            cursorPos.setX(c->geo.frame.left());
        } else if (test_data.edges & Qt::RightEdge) {
            cursorPos.setX(c->geo.frame.right());
        } else {
            cursorPos.setX(c->geo.frame.center().x());
        }

        if (test_data.edges & Qt::TopEdge) {
            cursorPos.setY(c->geo.frame.top());
        } else if (test_data.edges & Qt::BottomEdge) {
            cursorPos.setY(c->geo.frame.bottom());
        } else {
            cursorPos.setY(c->geo.frame.center().y());
        }

        cursor()->set_pos(cursorPos);

        const PlatformCursorImage arrowCursor = loadReferenceThemeCursor(Qt::ArrowCursor);
        QVERIFY(!arrowCursor.image().isNull());
        QCOMPARE(cursor()->platform_image().image(), arrowCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), arrowCursor.hotSpot());

        // start resizing the client
        int timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        pointer_button_pressed(BTN_RIGHT, timestamp++);
        QVERIFY(win::is_resize(c));

        auto const resizeCursor = loadReferenceThemeCursor(test_data.cursor_shape);
        QVERIFY(!resizeCursor.image().isNull());
        QCOMPARE(cursor()->platform_image().image(), resizeCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), resizeCursor.hotSpot());

        // finish resizing the client
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        pointer_button_released(BTN_RIGHT, timestamp++);
        QVERIFY(!win::is_resize(c));

        QCOMPARE(cursor()->platform_image().image(), arrowCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), arrowCursor.hotSpot());
    }

    SECTION("move cursor")
    {
        // this test verifies that the cursor has correct shape during move operation

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAll1", "Move");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->commandAllModifier(), Qt::MetaModifier);
        QCOMPARE(setup.base->options->qobject->commandAll1(),
                 base::options_qobject::MouseUnrestrictedMove);

        // create a test client
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // move cursor to the test position
        cursor()->set_pos(c->geo.frame.center());

        const PlatformCursorImage arrowCursor = loadReferenceThemeCursor(Qt::ArrowCursor);
        QVERIFY(!arrowCursor.image().isNull());
        QCOMPARE(cursor()->platform_image().image(), arrowCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), arrowCursor.hotSpot());

        // start moving the client
        int timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(win::is_move(c));

        const PlatformCursorImage sizeAllCursor = loadReferenceThemeCursor(Qt::SizeAllCursor);
        QVERIFY(!sizeAllCursor.image().isNull());
        QCOMPARE(cursor()->platform_image().image(), sizeAllCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), sizeAllCursor.hotSpot());

        // finish moving the client
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(!win::is_move(c));

        QCOMPARE(cursor()->platform_image().image(), arrowCursor.image());
        QCOMPARE(cursor()->platform_image().hotSpot(), arrowCursor.hotSpot());
    }

    SECTION("hide show cursor")
    {
        auto cursor = test::cursor();
        QCOMPARE(cursor->is_hidden(), false);
        cursor->hide();
        QCOMPARE(cursor->is_hidden(), true);
        cursor->show();
        QCOMPARE(cursor->is_hidden(), false);

        cursor->hide();
        QCOMPARE(cursor->is_hidden(), true);
        cursor->hide();
        cursor->hide();
        cursor->hide();
        QCOMPARE(cursor->is_hidden(), true);

        cursor->show();
        QCOMPARE(cursor->is_hidden(), true);
        cursor->show();
        QCOMPARE(cursor->is_hidden(), true);
        cursor->show();
        QCOMPARE(cursor->is_hidden(), true);
        cursor->show();
        QCOMPARE(cursor->is_hidden(), false);
    }
}

}

#include "pointer_input.moc"
