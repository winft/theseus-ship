/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/active_window.h"
#include "win/controlling.h"
#include "win/input.h"
#include "win/rules/book.h"
#include "win/rules/ruling.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/virtual_desktops.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("xdg-shell rules", "[win]")
{
    test::setup setup("xdg-shell-rules");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::xdg_decoration);

    auto& vd_manager = setup.base->space->virtual_desktop_manager;
    vd_manager->setCurrent(vd_manager->desktops().first());

    auto get_config = [&]() -> std::tuple<KSharedConfigPtr, KConfigGroup> {
        auto config = setup.base->config.main;

        auto group = config->group("1");
        group.deleteGroup();
        config->group("General").writeEntry("count", 1);
        return {config, group};
    };

    auto createWindow = [&](const QByteArray& appId,
                            int timeout = 5000) -> std::tuple<wayland_window*,
                                                              std::unique_ptr<Surface>,
                                                              std::unique_ptr<XdgShellToplevel>> {
        // Create an xdg surface.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);

        // Assign the desired app id.
        shellSurface->setAppId(appId);

        // Wait for the initial configure event.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);
        surface->commit(Surface::CommitFlag::None);
        configureRequestedSpy.wait();

        // Draw content of the surface.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());

        auto client = render_and_wait_for_shown(
            surface, QSize(100, 50), Qt::blue, QImage::Format_ARGB32, timeout);
        return {client, std::move(surface), std::move(shellSurface)};
    };

    auto get_toplevel_window = [&](QSignalSpy const& spy) -> wayland_window* {
        auto xdg_toplevel = spy.last().at(0).value<Wrapland::Server::XdgShellToplevel*>();
        for (auto win : setup.base->space->windows) {
            if (!std::holds_alternative<wayland_window*>(win)) {
                continue;
            }
            auto wlwin = std::get<wayland_window*>(win);
            if (wlwin->toplevel == xdg_toplevel) {
                return wlwin;
            }
        }
        return nullptr;
    };

    SECTION("position dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The position of the client should not be affected by the rule. The default
        // placement policy will put the client in the top-left corner of the screen.
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(0, 0));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("position apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The client should be moved to the position specified by the rule.
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // One should still be able to move the client around.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_move(*setup.base->space);
        QCOMPARE(get_wayland_window(setup.base->space->move_resize_window), client);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QVERIFY(win::is_move(client));
        QVERIFY(!win::is_resize(client));

        auto const cursorPos = cursor()->pos();
        win::key_press_event(client, Qt::Key_Right);
        win::update_move_resize(client, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        win::key_press_event(client, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        // The rule should be applied again if the client appears after it's been closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("position remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The client should be moved to the position specified by the rule.
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // One should still be able to move the client around.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_move(*setup.base->space);
        QCOMPARE(get_wayland_window(setup.base->space->move_resize_window), client);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QVERIFY(win::is_move(client));
        QVERIFY(!win::is_resize(client));

        auto const cursorPos = cursor()->pos();
        win::key_press_event(client, Qt::Key_Right);
        win::update_move_resize(client, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        win::key_press_event(client, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        // The client should be placed at the last know position if we reopen it.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("position force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The client should be moved to the position specified by the rule.
        QVERIFY(!client->isMovable());
        QVERIFY(!client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // User should not be able to move the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_move(*setup.base->space);
        QVERIFY(!setup.base->space->move_resize_window);
        QCOMPARE(clientStartMoveResizedSpy.count(), 0);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));

        // The position should still be forced if we reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isMovable());
        QVERIFY(!client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("position apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The position of the client isn't set by any rule, thus the default placement
        // policy will try to put the client in the top-left corner of the screen.
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(0, 0));

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;

        // The client should be moved to the position specified by the rule.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // We still have to be able to move the client around.
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_move(*setup.base->space);
        QCOMPARE(get_wayland_window(setup.base->space->move_resize_window), client);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QVERIFY(win::is_move(client));
        QVERIFY(!win::is_resize(client));

        auto const cursorPos = cursor()->pos();
        win::key_press_event(client, Qt::Key_Right);
        win::update_move_resize(client, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        win::key_press_event(client, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QCOMPARE(client->geo.pos(), QPoint(50, 42));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("position force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("position", QPoint(42, 42));
        group.writeEntry("positionrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The client should be moved to the position specified by the rule.
        QVERIFY(!client->isMovable());
        QVERIFY(!client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(42, 42));

        // User should not be able to move the client.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));

        win::active_window_move(*setup.base->space);
        QVERIFY(!setup.base->space->move_resize_window);
        QCOMPARE(clientStartMoveResizedSpy.count(), 0);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));

        // The rule should be discarded if we close the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMovable());
        QVERIFY(client->isMovableAcrossScreens());
        QCOMPARE(client->geo.pos(), QPoint(0, 0));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        std::unique_ptr<Surface> surface = create_surface();
        std::unique_ptr<XdgShellToplevel> shellSurface
            = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);

        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));

        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The window size shouldn't be enforced by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(100, 50));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        std::unique_ptr<XdgShellToplevel> shellSurface
            = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The initial configure event should contain size hint set by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // One still should be able to resize the client.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_resize(*setup.base->space);
        QCOMPARE(get_wayland_window(setup.base->space->move_resize_window), client);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QVERIFY(!win::is_move(client));
        QVERIFY(win::is_resize(client));
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());

        auto const cursorPos = cursor()->pos();
        win::key_press_event(client, Qt::Key_Right);
        win::update_move_resize(client, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 4);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));
        QCOMPARE(cfgdata.size, QSize(488, 640));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(488, 640), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(488, 640));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        win::key_press_event(client, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));

        // Interactive resize is not spec-compliant
        REQUIRE_FALSE(configureRequestedSpy->wait(10));
        REQUIRE_FALSE(configureRequestedSpy->count() == 5);

        // The rule should be applied again if the client appears after it's been closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The initial configure event should contain size hint set by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::resizing));

        // One should still be able to resize the client.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                                 &win::window_qobject::clientStepUserMovedResized);
        QVERIFY(clientStepUserMovedResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            client->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_resize(*setup.base->space);
        QCOMPARE(get_wayland_window(setup.base->space->move_resize_window), client);
        QCOMPARE(clientStartMoveResizedSpy.count(), 1);
        QVERIFY(!win::is_move(client));
        QVERIFY(win::is_resize(client));
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());

        auto const cursorPos = cursor()->pos();
        win::key_press_event(client, Qt::Key_Right);
        win::update_move_resize(client, cursor()->pos());
        QCOMPARE(cursor()->pos(), cursorPos + QPoint(8, 0));
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 4);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::resizing));
        QVERIFY(cfgdata.updates.testFlag(xdg_shell_toplevel_configure_change::size));
        QCOMPARE(cfgdata.size, QSize(488, 640));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(488, 640), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(488, 640));
        QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

        win::key_press_event(client, Qt::Key_Enter);
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));

        // Interactive resize is not spec-compliant
        REQUIRE_FALSE(configureRequestedSpy->wait(10));
        REQUIRE_FALSE(configureRequestedSpy->count() == 5);

        // If the client appears again, it should have the last known size.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(488, 640));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(488, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(488, 640));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The initial configure event should contain size hint set by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Any attempt to resize the client should not succeed.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_resize(*setup.base->space);
        QVERIFY(!setup.base->space->move_resize_window);
        QCOMPARE(clientStartMoveResizedSpy.count(), 0);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        QVERIFY(!configureRequestedSpy->wait(100));

        // If the client appears again, the size should still be forced.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size apply now")
    {
        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The expected surface dimensions should be set by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(100, 50));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The compositor should send a configure event with a new size.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));

        // Draw the surface with the new size.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(480, 640), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(480, 640));
        QVERIFY(!configureRequestedSpy->wait(100));

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!configureRequestedSpy->wait(100));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("size force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("size", QSize(480, 640));
        group.writeEntry("sizerule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // The initial configure event should contain size hint set by the rule.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(480, 640));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isResizable());
        QCOMPARE(client->geo.size(), QSize(480, 640));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();

        // Any attempt to resize the client should not succeed.
        QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(clientStartMoveResizedSpy.isValid());
        QVERIFY(!setup.base->space->move_resize_window);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        win::active_window_resize(*setup.base->space);
        QVERIFY(!setup.base->space->move_resize_window);
        QCOMPARE(clientStartMoveResizedSpy.count(), 0);
        QVERIFY(!win::is_move(client));
        QVERIFY(!win::is_resize(client));
        QVERIFY(!configureRequestedSpy->wait(100));

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isResizable());
        QCOMPARE(client->geo.size(), QSize(100, 50));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
        QCOMPARE(client->geo.size(), QSize(100, 50));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::apply));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // One should still be able to change the maximized state of the client.
        win::active_window_maximize(*setup.base->space);
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();

        // The size is empty since we did not have a restore size before.
        QVERIFY(cfgdata.size.isEmpty());
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(100, 50));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

        // If we create the client again, it should be initially maximized.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::remember));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // One should still be able to change the maximized state of the client.
        win::active_window_maximize(*setup.base->space);
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();

        // The size is empty since we did not have a restore size before.
        QVERIFY(cfgdata.size.isEmpty());
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(100, 50));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

        // If we create the client again, it should not be maximized (because last time it wasn't).
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
        QCOMPARE(client->geo.size(), QSize(100, 50));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::force));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Any attempt to change the maximized state should not succeed.
        const QRect oldGeometry = client->geo.frame;
        win::active_window_maximize(*setup.base->space);
        QVERIFY(!configureRequestedSpy->wait(100));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.frame, oldGeometry);

        // If we create the client again, the maximized state should still be forced.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize apply now")
    {
        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
        QCOMPARE(client->geo.size(), QSize(100, 50));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // We should receive a configure event with a new surface size.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Draw contents of the maximized client.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(1280, 1024));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);

        // The client still has to be maximizeable.
        QVERIFY(client->isMaximizable());

        // Restore the client.
        win::active_window_maximize(*setup.base->space);
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 4);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(100, 50));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(client->geo.size(), QSize(100, 50));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

        // The rule should be discarded after it's been applied.
        const QRect oldGeometry = client->geo.frame;
        win::rules::evaluate_rules(client);
        QVERIFY(!configureRequestedSpy->wait(100));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
        QCOMPARE(client->geo.frame, oldGeometry);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("maximize force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("maximizehoriz", true);
        group.writeEntry("maximizehorizrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        std::unique_ptr<QSignalSpy> configureRequestedSpy;
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        // Wait for the initial configure event.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Map the client.
        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(!client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.size(), QSize(1280, 1024));

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Any attempt to change the maximized state should not succeed.
        const QRect oldGeometry = client->geo.frame;
        win::active_window_maximize(*setup.base->space);
        QVERIFY(!configureRequestedSpy->wait(100));
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
        QCOMPARE(client->geo.frame, oldGeometry);

        // The rule should be discarded if we close the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        surface = create_surface();
        shellSurface = create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly);
        configureRequestedSpy.reset(
            new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configured));
        shellSurface->setAppId("org.kde.foo");
        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 1);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        shellSurface->ackConfigure(configureRequestedSpy->back().front().value<quint32>());
        client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QVERIFY(client->isMaximizable());
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
        QCOMPARE(client->geo.size(), QSize(100, 50));

        QVERIFY(configureRequestedSpy->wait());
        QCOMPARE(configureRequestedSpy->count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop dont affect")
    {
        // We need at least two virtual desktop for this test.
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should appear on the current virtual desktop.
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop apply")
    {
        // We need at least two virtual desktop for this test.
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should appear on the second virtual desktop.
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // We still should be able to move the client between desktops.
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 2);

        // If we re-open the client, it should appear on the second virtual desktop again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop remember")
    {
        // We need at least two virtual desktop for this test.
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // Move the client to the first virtual desktop.
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 2);

        // If we create the client again, it should appear on the first virtual desktop.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop force")
    {
        // We need at least two virtual desktop for this test.
        auto& vd_manager = setup.base->space->virtual_desktop_manager;
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should appear on the second virtual desktop.
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // Any attempt to move the client to another virtual desktop should fail.
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // If we re-open the client, it should appear on the second virtual desktop again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop apply now")
    {
        // We need at least two virtual desktop for this test.
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should have been moved to the second virtual desktop.
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 1);

        // One should still be able to move the client between desktops.
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop force temporarily")
    {
        // We need at least two virtual desktop for this test.
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("desktops", QStringList{vd_manager->desktopForX11Id(2)->id()});
        group.writeEntry("desktopsrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should appear on the second virtual desktop.
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // Any attempt to move the client to another virtual desktop should fail.
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 2);

        // The rule should be discarded when the client is withdrawn.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        vd_manager->setCurrent(1);
        QCOMPARE(vd_manager->current(), 1);
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // One should be able to move the client between desktops.
        win::send_window_to_desktop(*setup.base->space, client, 2, true);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(vd_manager->current(), 1);
        win::send_window_to_desktop(*setup.base->space, client, 1, true);
        QCOMPARE(win::get_desktop(*client), 1);
        QCOMPARE(vd_manager->current(), 1);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", true);
        group.writeEntry("minimizerule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->isMinimizable());

        // The client should not be minimized.
        QVERIFY(!client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", true);
        group.writeEntry("minimizerule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        QSignalSpy toplevel_created_Spy(setup.base->space->xdg_shell.get(),
                                        &Wrapland::Server::XdgShell::toplevelCreated);
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo", 500);
        QVERIFY(!client);
        QCOMPARE(toplevel_created_Spy.size(), 1);

        client = get_toplevel_window(toplevel_created_Spy);
        QVERIFY(client);
        QVERIFY(client->isMinimizable());

        // The client should be minimized.
        QVERIFY(client->control->minimized);

        // We should still be able to unminimize the client.
        win::set_minimized(client, false);
        QVERIFY(!client->control->minimized);

        // If we re-open the client, it should be minimized back again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));

        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo", 500);
        QVERIFY(!client);
        QCOMPARE(toplevel_created_Spy.size(), 2);

        client = get_toplevel_window(toplevel_created_Spy);
        QVERIFY(client);
        QVERIFY(client->isMinimizable());
        QVERIFY(client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", false);
        group.writeEntry("minimizerule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->isMinimizable());
        QVERIFY(!client->control->minimized);

        // Minimize the client.
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);

        // If we open the client again, it should be minimized.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));

        QSignalSpy toplevel_created_Spy(setup.base->space->xdg_shell.get(),
                                        &Wrapland::Server::XdgShell::toplevelCreated);
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo", 500);
        QVERIFY(!client);
        QCOMPARE(toplevel_created_Spy.size(), 1);

        client = get_toplevel_window(toplevel_created_Spy);
        QVERIFY(client);

        QVERIFY(client->isMinimizable());
        QVERIFY(client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", false);
        group.writeEntry("minimizerule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->isMinimizable());
        QVERIFY(!client->control->minimized);

        // Any attempt to minimize the client should fail.
        win::set_minimized(client, true);
        QVERIFY(!client->control->minimized);

        // If we re-open the client, the minimized state should still be forced.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->isMinimizable());
        QVERIFY(!client->control->minimized);
        win::set_minimized(client, true);
        QVERIFY(!client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->isMinimizable());
        QVERIFY(!client->control->minimized);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", true);
        group.writeEntry("minimizerule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should be minimized now.
        QVERIFY(client->isMinimizable());
        QVERIFY(client->control->minimized);

        // One is still able to unminimize the client.
        win::set_minimized(client, false);
        QVERIFY(!client->control->minimized);

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(client->isMinimizable());
        QVERIFY(!client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("minimize force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("minimize", false);
        group.writeEntry("minimizerule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->isMinimizable());
        QVERIFY(!client->control->minimized);

        // Any attempt to minimize the client should fail until the client is closed.
        win::set_minimized(client, true);
        QVERIFY(!client->control->minimized);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->isMinimizable());
        QVERIFY(!client->control->minimized);
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be affected by the rule.
        QVERIFY(!client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a taskbar.
        QVERIFY(client->control->skip_taskbar());

        // Though one can change that.
        win::set_original_skip_taskbar(client, false);
        QVERIFY(!client->control->skip_taskbar());

        // Reopen the client, the rule should be applied again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a taskbar.
        QVERIFY(client->control->skip_taskbar());

        // Change the skip-taskbar state.
        win::set_original_skip_taskbar(client, false);
        QVERIFY(!client->control->skip_taskbar());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be included on a taskbar.
        QVERIFY(!client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a taskbar.
        QVERIFY(client->control->skip_taskbar());

        // Any attempt to change the skip-taskbar state should not succeed.
        win::set_original_skip_taskbar(client, false);
        QVERIFY(client->control->skip_taskbar());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The skip-taskbar state should be still forced.
        QVERIFY(client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_taskbar());

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should not be on a taskbar now.
        QVERIFY(client->control->skip_taskbar());

        // Also, one change the skip-taskbar state.
        win::set_original_skip_taskbar(client, false);
        QVERIFY(!client->control->skip_taskbar());

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip taskbar force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skiptaskbar", true);
        group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a taskbar.
        QVERIFY(client->control->skip_taskbar());

        // Any attempt to change the skip-taskbar state should not succeed.
        win::set_original_skip_taskbar(client, false);
        QVERIFY(client->control->skip_taskbar());

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_taskbar());

        // The skip-taskbar state is no longer forced.
        win::set_original_skip_taskbar(client, true);
        QVERIFY(client->control->skip_taskbar());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be affected by the rule.
        QVERIFY(!client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a pager.
        QVERIFY(client->control->skip_pager());

        // Though one can change that.
        win::set_skip_pager(client, false);
        QVERIFY(!client->control->skip_pager());

        // Reopen the client, the rule should be applied again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a pager.
        QVERIFY(client->control->skip_pager());

        // Change the skip-pager state.
        win::set_skip_pager(client, false);
        QVERIFY(!client->control->skip_pager());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be included on a pager.
        QVERIFY(!client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a pager.
        QVERIFY(client->control->skip_pager());

        // Any attempt to change the skip-pager state should not succeed.
        win::set_skip_pager(client, false);
        QVERIFY(client->control->skip_pager());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The skip-pager state should be still forced.
        QVERIFY(client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_pager());

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should not be on a pager now.
        QVERIFY(client->control->skip_pager());

        // Also, one change the skip-pager state.
        win::set_skip_pager(client, false);
        QVERIFY(!client->control->skip_pager());

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip pager force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skippager", true);
        group.writeEntry("skippagerrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be included on a pager.
        QVERIFY(client->control->skip_pager());

        // Any attempt to change the skip-pager state should not succeed.
        win::set_skip_pager(client, false);
        QVERIFY(client->control->skip_pager());

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_pager());

        // The skip-pager state is no longer forced.
        win::set_skip_pager(client, true);
        QVERIFY(client->control->skip_pager());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should not be affected by the rule.
        QVERIFY(!client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be excluded from window switching effects.
        QVERIFY(client->control->skip_switcher());

        // Though one can change that.
        win::set_skip_switcher(client, false);
        QVERIFY(!client->control->skip_switcher());

        // Reopen the client, the rule should be applied again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be excluded from window switching effects.
        QVERIFY(client->control->skip_switcher());

        // Change the skip-switcher state.
        win::set_skip_switcher(client, false);
        QVERIFY(!client->control->skip_switcher());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be included in window switching effects.
        QVERIFY(!client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be excluded from window switching effects.
        QVERIFY(client->control->skip_switcher());

        // Any attempt to change the skip-switcher state should not succeed.
        win::set_skip_switcher(client, false);
        QVERIFY(client->control->skip_switcher());

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The skip-switcher state should be still forced.
        QVERIFY(client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_switcher());

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should be excluded from window switching effects now.
        QVERIFY(client->control->skip_switcher());

        // Also, one change the skip-switcher state.
        win::set_skip_switcher(client, false);
        QVERIFY(!client->control->skip_switcher());

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("skip switcher force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("skipswitcher", true);
        group.writeEntry("skipswitcherrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The client should be excluded from window switching effects.
        QVERIFY(client->control->skip_switcher());

        // Any attempt to change the skip-switcher state should not succeed.
        win::set_skip_switcher(client, false);
        QVERIFY(client->control->skip_switcher());

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->skip_switcher());

        // The skip-switcher state is no longer forced.
        win::set_skip_switcher(client, true);
        QVERIFY(client->control->skip_switcher());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The keep-above state of the client should not be affected by the rule.
        QVERIFY(!client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept above.
        QVERIFY(client->control->keep_above);

        // One should also be able to alter the keep-above state.
        win::set_keep_above(client, false);
        QVERIFY(!client->control->keep_above);

        // If one re-opens the client, it should be kept above back again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept above.
        QVERIFY(client->control->keep_above);

        // Unset the keep-above state.
        win::set_keep_above(client, false);
        QVERIFY(!client->control->keep_above);
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));

        // Re-open the client, it should not be kept above.
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept above.
        QVERIFY(client->control->keep_above);

        // Any attemt to unset the keep-above should not succeed.
        win::set_keep_above(client, false);
        QVERIFY(client->control->keep_above);

        // If we re-open the client, it should still be kept above.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_above);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should now be kept above other clients.
        QVERIFY(client->control->keep_above);

        // One is still able to change the keep-above state of the client.
        win::set_keep_above(client, false);
        QVERIFY(!client->control->keep_above);

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep above force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept above.
        QVERIFY(client->control->keep_above);

        // Any attempt to alter the keep-above state should not succeed.
        win::set_keep_above(client, false);
        QVERIFY(client->control->keep_above);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_above);

        // The keep-above state is no longer forced.
        win::set_keep_above(client, true);
        QVERIFY(client->control->keep_above);
        win::set_keep_above(client, false);
        QVERIFY(!client->control->keep_above);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The keep-below state of the client should not be affected by the rule.
        QVERIFY(!client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept below.
        QVERIFY(client->control->keep_below);

        // One should also be able to alter the keep-below state.
        win::set_keep_below(client, false);
        QVERIFY(!client->control->keep_below);

        // If one re-opens the client, it should be kept above back again.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below remember")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept below.
        QVERIFY(client->control->keep_below);

        // Unset the keep-below state.
        win::set_keep_below(client, false);
        QVERIFY(!client->control->keep_below);
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));

        // Re-open the client, it should not be kept below.
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept below.
        QVERIFY(client->control->keep_below);

        // Any attemt to unset the keep-below should not succeed.
        win::set_keep_below(client, false);
        QVERIFY(client->control->keep_below);

        // If we re-open the client, it should still be kept below.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_below);

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should now be kept below other clients.
        QVERIFY(client->control->keep_below);

        // One is still able to change the keep-below state of the client.
        win::set_keep_below(client, false);
        QVERIFY(!client->control->keep_below);

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QVERIFY(!client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("keep below force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("below", true);
        group.writeEntry("belowrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // Initially, the client should be kept below.
        QVERIFY(client->control->keep_below);

        // Any attempt to alter the keep-below state should not succeed.
        win::set_keep_below(client, false);
        QVERIFY(client->control->keep_below);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(!client->control->keep_below);

        // The keep-below state is no longer forced.
        win::set_keep_below(client, true);
        QVERIFY(client->control->keep_below);
        win::set_keep_below(client, false);
        QVERIFY(!client->control->keep_below);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QCOMPARE(client->control->shortcut, QKeySequence());
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);

        // If we press the window shortcut, nothing should happen.
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(!clientUnminimizedSpy.wait(100));
        QVERIFY(client->control->minimized);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut apply")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::apply));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // If we press the window shortcut, the window should be brought back to user.
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // One can also change the shortcut.
        win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_2}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        keyboard_key_released(KEY_2, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // The old shortcut should do nothing.
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(!clientUnminimizedSpy.wait(100));
        QVERIFY(client->control->minimized);

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The window shortcut should be set back to Ctrl+Alt+1.
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut remember")
    {
        QSKIP("KWin core doesn't try to save the last used window shortcut");

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::remember));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // If we press the window shortcut, the window should be brought back to user.
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // Change the window shortcut to Ctrl+Alt+2.
        win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_2}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        keyboard_key_released(KEY_2, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The window shortcut should be set to the last known value.
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_2}));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut force")
    {
        QSKIP("KWin core can't release forced window shortcuts");

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // If we press the window shortcut, the window should be brought back to user.
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // Any attempt to change the window shortcut should not succeed.
        win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        keyboard_key_released(KEY_2, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(!clientUnminimizedSpy.wait(100));
        QVERIFY(client->control->minimized);

        // Reopen the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // The window shortcut should still be forced.
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut apply now")
    {
        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->shortcut.isEmpty());

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::apply_now));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // The client should now have a window shortcut assigned.
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // Assign a different shortcut.
        win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_2}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        keyboard_key_released(KEY_2, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // The rule should not be applied again.
        win::rules::evaluate_rules(client);
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_2}));

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("shortcut force temporarily")
    {
        QSKIP("KWin core can't release forced window shortcuts");

        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("shortcut", "Ctrl+Alt+1");
        group.writeEntry("shortcutrule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);

        // If we press the window shortcut, the window should be brought back to user.
        QSignalSpy clientUnminimizedSpy(client->qobject.get(),
                                        &win::window_qobject::clientUnminimized);
        QVERIFY(clientUnminimizedSpy.isValid());
        quint32 timestamp = 1;
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_1, timestamp++);
        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(clientUnminimizedSpy.wait());
        QVERIFY(!client->control->minimized);

        // Any attempt to change the window shortcut should not succeed.
        win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
        QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_1}));
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        keyboard_key_released(KEY_2, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        QVERIFY(!clientUnminimizedSpy.wait(100));
        QVERIFY(client->control->minimized);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->shortcut.isEmpty());

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("desktop file dont affect")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("desktop file apply")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("desktop file remember")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("desktop file force")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("desktop file apply now")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("desktop file force temporarily")
    {
        // Currently, the desktop file name is derived from the app id. If the app id is
        // changed, then the old rules will be lost. Either setDesktopFileName should
        // be exposed or the desktop file name rule should be removed for wayland clients.
        QSKIP("Needs changes in KWin core to pass");
    }

    SECTION("active opacity dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityactive", 90);
        group.writeEntry("opacityactiverule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // The opacity should not be affected by the rule.
        QCOMPARE(client->opacity(), 1.0);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("active opacity force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityactive", 90);
        group.writeEntry("opacityactiverule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 0.9);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("active opacity force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityactive", 90);
        group.writeEntry("opacityactiverule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 0.9);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 1.0);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("inactive opacity dont affect")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityinactive", 80);
        group.writeEntry("opacityinactiverule", enum_index(win::rules::action::dont_affect));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);

        // Make the client inactive.
        win::unset_active_window(*setup.base->space);
        QVERIFY(!client->control->active);

        // The opacity of the client should not be affected by the rule.
        QCOMPARE(client->opacity(), 1.0);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("inactive opacity force")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityinactive", 80);
        group.writeEntry("opacityinactiverule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 1.0);

        // Make the client inactive.
        win::unset_active_window(*setup.base->space);
        QVERIFY(!client->control->active);

        // The opacity should be forced by the rule.
        QCOMPARE(client->opacity(), 0.8);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("inactive opacity force temporarily")
    {
        // Initialize RuleBook with the test rule.
        auto [config, group] = get_config();
        group.writeEntry("opacityinactive", 80);
        group.writeEntry("opacityinactiverule", enum_index(win::rules::action::force_temporarily));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        // Create the test client.
        wayland_window* client;
        std::unique_ptr<Surface> surface;
        std::unique_ptr<XdgShellToplevel> shellSurface;
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 1.0);

        // Make the client inactive.
        win::unset_active_window(*setup.base->space);
        QVERIFY(!client->control->active);

        // The opacity should be forced by the rule.
        QCOMPARE(client->opacity(), 0.8);

        // The rule should be discarded when the client is closed.
        shellSurface.reset();
        surface.reset();
        std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->opacity(), 1.0);
        win::unset_active_window(*setup.base->space);
        QVERIFY(!client->control->active);
        QCOMPARE(client->opacity(), 1.0);

        // Destroy the client.
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }

    SECTION("match after name change")
    {
        auto [config, group] = get_config();
        group.writeEntry("above", true);
        group.writeEntry("aboverule", enum_index(win::rules::action::force));
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();

        setup.base->space->rule_book->config = config;
        win::space_reconfigure(*setup.base->space);

        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface);

        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QVERIFY(c->control->active);
        QCOMPARE(c->control->keep_above, false);

        QSignalSpy desktopFileNameSpy(c->qobject.get(),
                                      &win::window_qobject::desktopFileNameChanged);
        QVERIFY(desktopFileNameSpy.isValid());

        shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
        QVERIFY(desktopFileNameSpy.wait());
        QCOMPARE(c->control->keep_above, true);
    }
}

}
