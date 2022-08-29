/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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

namespace KWin
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

class TestXdgShellClientRules : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testPositionDontAffect();
    void testPositionApply();
    void testPositionRemember();
    void testPositionForce();
    void testPositionApplyNow();
    void testPositionForceTemporarily();

    void testSizeDontAffect();
    void testSizeApply();
    void testSizeRemember();
    void testSizeForce();
    void testSizeApplyNow();
    void testSizeForceTemporarily();

    void testMaximizeDontAffect();
    void testMaximizeApply();
    void testMaximizeRemember();
    void testMaximizeForce();
    void testMaximizeApplyNow();
    void testMaximizeForceTemporarily();

    void testDesktopDontAffect();
    void testDesktopApply();
    void testDesktopRemember();
    void testDesktopForce();
    void testDesktopApplyNow();
    void testDesktopForceTemporarily();

    void testMinimizeDontAffect();
    void testMinimizeApply();
    void testMinimizeRemember();
    void testMinimizeForce();
    void testMinimizeApplyNow();
    void testMinimizeForceTemporarily();

    void testSkipTaskbarDontAffect();
    void testSkipTaskbarApply();
    void testSkipTaskbarRemember();
    void testSkipTaskbarForce();
    void testSkipTaskbarApplyNow();
    void testSkipTaskbarForceTemporarily();

    void testSkipPagerDontAffect();
    void testSkipPagerApply();
    void testSkipPagerRemember();
    void testSkipPagerForce();
    void testSkipPagerApplyNow();
    void testSkipPagerForceTemporarily();

    void testSkipSwitcherDontAffect();
    void testSkipSwitcherApply();
    void testSkipSwitcherRemember();
    void testSkipSwitcherForce();
    void testSkipSwitcherApplyNow();
    void testSkipSwitcherForceTemporarily();

    void testKeepAboveDontAffect();
    void testKeepAboveApply();
    void testKeepAboveRemember();
    void testKeepAboveForce();
    void testKeepAboveApplyNow();
    void testKeepAboveForceTemporarily();

    void testKeepBelowDontAffect();
    void testKeepBelowApply();
    void testKeepBelowRemember();
    void testKeepBelowForce();
    void testKeepBelowApplyNow();
    void testKeepBelowForceTemporarily();

    void testShortcutDontAffect();
    void testShortcutApply();
    void testShortcutRemember();
    void testShortcutForce();
    void testShortcutApplyNow();
    void testShortcutForceTemporarily();

    void testDesktopFileDontAffect();
    void testDesktopFileApply();
    void testDesktopFileRemember();
    void testDesktopFileForce();
    void testDesktopFileApplyNow();
    void testDesktopFileForceTemporarily();

    void testActiveOpacityDontAffect();
    void testActiveOpacityForce();
    void testActiveOpacityForceTemporarily();

    void testInactiveOpacityDontAffect();
    void testInactiveOpacityForce();
    void testInactiveOpacityForceTemporarily();

    void testMatchAfterNameChange();
};

void TestXdgShellClientRules::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TestXdgShellClientRules::init()
{
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCurrent(vd_manager->desktops().first());
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);
}

void TestXdgShellClientRules::cleanup()
{
    Test::destroy_wayland_connection();

    // Unreference the previous config.
    Test::app()->base.space->rule_book->config = {};
    win::space_reconfigure(*Test::app()->base.space);

    // Restore virtual desktops to the initial state.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCount(1);
    QCOMPARE(vd_manager->count(), 1u);
}

std::tuple<wayland_window*, std::unique_ptr<Surface>, std::unique_ptr<XdgShellToplevel>>
createWindow(const QByteArray& appId, int timeout = 5000)
{
    // Create an xdg surface.
    auto surface = Test::create_surface();
    auto shellSurface = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);

    // Assign the desired app id.
    shellSurface->setAppId(appId);

    // Wait for the initial configure event.
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    surface->commit(Surface::CommitFlag::None);
    configureRequestedSpy.wait();

    // Draw content of the surface.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());

    auto client = Test::render_and_wait_for_shown(
        surface, QSize(100, 50), Qt::blue, QImage::Format_ARGB32, timeout);
    return {client, std::move(surface), std::move(shellSurface)};
}

wayland_window* get_toplevel_window(QSignalSpy const& spy)
{
    auto xdg_toplevel = spy.last().at(0).value<Wrapland::Server::XdgShellToplevel*>();
    for (auto win : Test::app()->base.space->windows) {
        if (auto wl_win = dynamic_cast<wayland_window*>(win);
            wl_win && wl_win->toplevel == xdg_toplevel) {
            return wl_win;
        }
    }
    return nullptr;
}

void TestXdgShellClientRules::testPositionDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QCOMPARE(client->pos(), QPoint(0, 0));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testPositionApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QCOMPARE(client->pos(), QPoint(42, 42));

    // One should still be able to move the client around.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_move(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(win::is_move(client));
    QVERIFY(!win::is_resize(client));

    auto const cursorPos = Test::app()->base.input->cursor->pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, Test::app()->base.input->cursor->pos());
    QCOMPARE(Test::app()->base.input->cursor->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    QCOMPARE(client->pos(), QPoint(50, 42));

    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    QCOMPARE(client->pos(), QPoint(50, 42));

    // The rule should be applied again if the client appears after it's been closed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMovable());
    QVERIFY(client->isMovableAcrossScreens());
    QCOMPARE(client->pos(), QPoint(42, 42));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testPositionRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QCOMPARE(client->pos(), QPoint(42, 42));

    // One should still be able to move the client around.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_move(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(win::is_move(client));
    QVERIFY(!win::is_resize(client));

    auto const cursorPos = Test::app()->base.input->cursor->pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, Test::app()->base.input->cursor->pos());
    QCOMPARE(Test::app()->base.input->cursor->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    QCOMPARE(client->pos(), QPoint(50, 42));

    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    QCOMPARE(client->pos(), QPoint(50, 42));

    // The client should be placed at the last know position if we reopen it.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMovable());
    QVERIFY(client->isMovableAcrossScreens());
    QCOMPARE(client->pos(), QPoint(50, 42));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testPositionForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QCOMPARE(client->pos(), QPoint(42, 42));

    // User should not be able to move the client.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_move(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QCOMPARE(clientStartMoveResizedSpy.count(), 0);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));

    // The position should still be forced if we reopen the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isMovable());
    QVERIFY(!client->isMovableAcrossScreens());
    QCOMPARE(client->pos(), QPoint(42, 42));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testPositionApplyNow()
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
    QCOMPARE(client->pos(), QPoint(0, 0));

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;

    // The client should be moved to the position specified by the rule.
    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    win::space_reconfigure(*Test::app()->base.space);
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(client->pos(), QPoint(42, 42));

    // We still have to be able to move the client around.
    QVERIFY(client->isMovable());
    QVERIFY(client->isMovableAcrossScreens());
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(client->qobject.get(),
                                             &win::window_qobject::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_move(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(win::is_move(client));
    QVERIFY(!win::is_resize(client));

    auto const cursorPos = Test::app()->base.input->cursor->pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, Test::app()->base.input->cursor->pos());
    QCOMPARE(Test::app()->base.input->cursor->pos(), cursorPos + QPoint(8, 0));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    QCOMPARE(client->pos(), QPoint(50, 42));

    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    QCOMPARE(client->pos(), QPoint(50, 42));

    // The rule should not be applied again.
    win::rules::evaluate_rules(client);
    QCOMPARE(client->pos(), QPoint(50, 42));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testPositionForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("position", QPoint(42, 42));
    group.writeEntry("positionrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QCOMPARE(client->pos(), QPoint(42, 42));

    // User should not be able to move the client.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_move(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QCOMPARE(clientStartMoveResizedSpy.count(), 0);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));

    // The rule should be discarded if we close the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMovable());
    QVERIFY(client->isMovableAcrossScreens());
    QCOMPARE(client->pos(), QPoint(0, 0));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    std::unique_ptr<Surface> surface = Test::create_surface();
    std::unique_ptr<XdgShellToplevel> shellSurface
        = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The window size shouldn't be enforced by the rule.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(0, 0));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(100, 50));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    std::unique_ptr<XdgShellToplevel> shellSurface
        = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The initial configure event should contain size hint set by the rule.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(480, 640));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

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
    QSignalSpy clientFinishUserMovedResizedSpy(client->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());
    QSignalSpy surfaceSizeChangedSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(surfaceSizeChangedSpy.isValid());

    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_resize(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(!win::is_move(client));
    QVERIFY(win::is_resize(client));
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());

    auto const cursorPos = Test::app()->base.input->cursor->pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, Test::app()->base.input->cursor->pos());
    QCOMPARE(Test::app()->base.input->cursor->pos(), cursorPos + QPoint(8, 0));
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 4);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(surfaceSizeChangedSpy.count(), 1);
    QCOMPARE(surfaceSizeChangedSpy.last().first().toSize(), QSize(488, 640));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(488, 640), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(488, 640));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));

    QEXPECT_FAIL("", "Interactive resize is not spec-compliant", Continue);
    QVERIFY(configureRequestedSpy->wait(10));
    QEXPECT_FAIL("", "Interactive resize is not spec-compliant", Continue);
    QCOMPARE(configureRequestedSpy->count(), 5);

    // The rule should be applied again if the client appears after it's been closed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The initial configure event should contain size hint set by the rule.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

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
    QSignalSpy clientFinishUserMovedResizedSpy(client->qobject.get(),
                                               &win::window_qobject::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());
    QSignalSpy surfaceSizeChangedSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(surfaceSizeChangedSpy.isValid());

    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_resize(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(!win::is_move(client));
    QVERIFY(win::is_resize(client));
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());

    auto const cursorPos = Test::app()->base.input->cursor->pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, Test::app()->base.input->cursor->pos());
    QCOMPARE(Test::app()->base.input->cursor->pos(), cursorPos + QPoint(8, 0));
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 4);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(surfaceSizeChangedSpy.count(), 1);
    QCOMPARE(surfaceSizeChangedSpy.last().first().toSize(), QSize(488, 640));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(488, 640), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(488, 640));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));

    QEXPECT_FAIL("", "Interactive resize is not spec-compliant", Continue);
    QVERIFY(configureRequestedSpy->wait(10));
    QEXPECT_FAIL("", "Interactive resize is not spec-compliant", Continue);
    QCOMPARE(configureRequestedSpy->count(), 5);

    // If the client appears again, it should have the last known size.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(488, 640));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(488, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(488, 640));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The initial configure event should contain size hint set by the rule.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Any attempt to resize the client should not succeed.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_resize(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QCOMPARE(clientStartMoveResizedSpy.count(), 0);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    QVERIFY(!configureRequestedSpy->wait(100));

    // If the client appears again, the size should still be forced.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeApplyNow()
{
    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The expected surface dimensions should be set by the rule.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(0, 0));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(100, 50));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // The compositor should send a configure event with a new size.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));

    // Draw the surface with the new size.
    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(480, 640), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(480, 640));
    QVERIFY(!configureRequestedSpy->wait(100));

    // The rule should not be applied again.
    win::rules::evaluate_rules(client);
    QVERIFY(!configureRequestedSpy->wait(100));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSizeForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("size", QSize(480, 640));
    group.writeEntry("sizerule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // The initial configure event should contain size hint set by the rule.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(480, 640));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(480, 640), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isResizable());
    QCOMPARE(client->size(), QSize(480, 640));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Any attempt to resize the client should not succeed.
    QSignalSpy clientStartMoveResizedSpy(client->qobject.get(),
                                         &win::window_qobject::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    win::active_window_resize(*Test::app()->base.space);
    QCOMPARE(Test::app()->base.space->move_resize_window, nullptr);
    QCOMPARE(clientStartMoveResizedSpy.count(), 0);
    QVERIFY(!win::is_move(client));
    QVERIFY(!win::is_resize(client));
    QVERIFY(!configureRequestedSpy->wait(100));

    // The rule should be discarded when the client is closed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().first().toSize(), QSize(0, 0));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isResizable());
    QCOMPARE(client->size(), QSize(100, 50));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(0, 0));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QCOMPARE(client->size(), QSize(100, 50));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::apply));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // One should still be able to change the maximized state of the client.
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);

    // The size is empty since we did not have a restore size before.
    QVERIFY(configureRequestedSpy->last().at(0).toSize().isEmpty());

    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(100, 50));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

    // If we create the client again, it should be initially maximized.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::remember));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // One should still be able to change the maximized state of the client.
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);

    // The size is empty since we did not have a restore size before.
    QVERIFY(configureRequestedSpy->last().at(0).toSize().isEmpty());

    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(100, 50));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

    // If we create the client again, it should not be maximized (because last time it wasn't).
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(0, 0));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QCOMPARE(client->size(), QSize(100, 50));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::force));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Any attempt to change the maximized state should not succeed.
    const QRect oldGeometry = client->frameGeometry();
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(!configureRequestedSpy->wait(100));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->frameGeometry(), oldGeometry);

    // If we create the client again, the maximized state should still be forced.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeApplyNow()
{
    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(0, 0));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QCOMPARE(client->size(), QSize(100, 50));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We should receive a configure event with a new surface size.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 3);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Draw contents of the maximized client.
    QSignalSpy geometryChangedSpy(client->qobject.get(),
                                  &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(1280, 1024));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);

    // The client still has to be maximizeable.
    QVERIFY(client->isMaximizable());

    // Restore the client.
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 4);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(100, 50));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->size(), QSize(100, 50));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);

    // The rule should be discarded after it's been applied.
    const QRect oldGeometry = client->frameGeometry();
    win::rules::evaluate_rules(client);
    QVERIFY(!configureRequestedSpy->wait(100));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QCOMPARE(client->frameGeometry(), oldGeometry);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMaximizeForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("maximizehoriz", true);
    group.writeEntry("maximizehorizrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("maximizevert", true);
    group.writeEntry("maximizevertrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    auto surface = Test::create_surface();
    auto shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    std::unique_ptr<QSignalSpy> configureRequestedSpy;
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(1280, 1024));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Map the client.
    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(1280, 1024), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(!client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Any attempt to change the maximized state should not succeed.
    const QRect oldGeometry = client->frameGeometry();
    win::active_window_maximize(*Test::app()->base.space);
    QVERIFY(!configureRequestedSpy->wait(100));
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::full);
    QCOMPARE(client->frameGeometry(), oldGeometry);

    // The rule should be discarded if we close the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    surface = Test::create_surface();
    shellSurface = create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    configureRequestedSpy.reset(
        new QSignalSpy(shellSurface.get(), &XdgShellToplevel::configureRequested));
    shellSurface->setAppId("org.kde.foo");
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 1);
    QCOMPARE(configureRequestedSpy->last().at(0).toSize(), QSize(0, 0));
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    shellSurface->ackConfigure(configureRequestedSpy->last().at(2).value<quint32>());
    client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active);
    QVERIFY(client->isMaximizable());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->synced_geometry.max_mode, win::maximize_mode::restore);
    QCOMPARE(client->size(), QSize(100, 50));

    QVERIFY(configureRequestedSpy->wait());
    QCOMPARE(configureRequestedSpy->count(), 2);
    states = configureRequestedSpy->last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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

    // The client should appear on the current virtual desktop.
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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

    // The client should appear on the second virtual desktop.
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // We still should be able to move the client between desktops.
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 2);

    // If we re-open the client, it should appear on the second virtual desktop again.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    vd_manager->setCurrent(1);
    QCOMPARE(vd_manager->current(), 1);
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // Move the client to the first virtual desktop.
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 2);

    // If we create the client again, it should appear on the first virtual desktop.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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

    // The client should appear on the second virtual desktop.
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // Any attempt to move the client to another virtual desktop should fail.
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // If we re-open the client, it should appear on the second virtual desktop again.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    vd_manager->setCurrent(1);
    QCOMPARE(vd_manager->current(), 1);
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopApplyNow()
{
    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // The client should have been moved to the second virtual desktop.
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 1);

    // One should still be able to move the client between desktops.
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // The rule should not be applied again.
    win::rules::evaluate_rules(client);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("desktop", 2);
    group.writeEntry("desktoprule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // We need at least two virtual desktop for this test.
    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
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

    // The client should appear on the second virtual desktop.
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // Any attempt to move the client to another virtual desktop should fail.
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 2);

    // The rule should be discarded when the client is withdrawn.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    vd_manager->setCurrent(1);
    QCOMPARE(vd_manager->current(), 1);
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // One should be able to move the client between desktops.
    win::send_window_to_desktop(*Test::app()->base.space, client, 2, true);
    QCOMPARE(client->desktop(), 2);
    QCOMPARE(vd_manager->current(), 1);
    win::send_window_to_desktop(*Test::app()->base.space, client, 1, true);
    QCOMPARE(client->desktop(), 1);
    QCOMPARE(vd_manager->current(), 1);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", true);
    group.writeEntry("minimizerule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", true);
    group.writeEntry("minimizerule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    QSignalSpy toplevel_created_Spy(Test::app()->base.space->xdg_shell.get(),
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
    QVERIFY(Test::wait_for_destroyed(client));

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", false);
    group.writeEntry("minimizerule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));

    QSignalSpy toplevel_created_Spy(Test::app()->base.space->xdg_shell.get(),
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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", false);
    group.writeEntry("minimizerule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->isMinimizable());
    QVERIFY(!client->control->minimized);
    win::set_minimized(client, true);
    QVERIFY(!client->control->minimized);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeApplyNow()
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
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", true);
    group.writeEntry("minimizerule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMinimizeForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("minimize", false);
    group.writeEntry("minimizerule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->isMinimizable());
    QVERIFY(!client->control->minimized);
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->skip_taskbar());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The client should be included on a taskbar.
    QVERIFY(!client->control->skip_taskbar());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The skip-taskbar state should be still forced.
    QVERIFY(client->control->skip_taskbar());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_taskbar());

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipTaskbarForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skiptaskbar", true);
    group.writeEntry("skiptaskbarrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_taskbar());

    // The skip-taskbar state is no longer forced.
    win::set_original_skip_taskbar(client, true);
    QVERIFY(client->control->skip_taskbar());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->skip_pager());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The client should be included on a pager.
    QVERIFY(!client->control->skip_pager());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The skip-pager state should be still forced.
    QVERIFY(client->control->skip_pager());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_pager());

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipPagerForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skippager", true);
    group.writeEntry("skippagerrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_pager());

    // The skip-pager state is no longer forced.
    win::set_skip_pager(client, true);
    QVERIFY(client->control->skip_pager());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->skip_switcher());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The client should be included in window switching effects.
    QVERIFY(!client->control->skip_switcher());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The skip-switcher state should be still forced.
    QVERIFY(client->control->skip_switcher());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_switcher());

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testSkipSwitcherForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("skipswitcher", true);
    group.writeEntry("skipswitcherrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->skip_switcher());

    // The skip-switcher state is no longer forced.
    win::set_skip_switcher(client, true);
    QVERIFY(client->control->skip_switcher());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->keep_above);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));

    // Re-open the client, it should not be kept above.
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->keep_above);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->keep_above);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->keep_above);

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepAboveForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->keep_below);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowRemember()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));

    // Re-open the client, it should not be kept below.
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->keep_below);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->keep_below);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(!client->control->keep_below);

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testKeepBelowForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("below", true);
    group.writeEntry("belowrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(!clientUnminimizedSpy.wait(100));
    QVERIFY(client->control->minimized);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutApply()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::apply));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // If we press the window shortcut, the window should be brought back to user.
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // One can also change the shortcut.
    win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_2}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // The old shortcut should do nothing.
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(!clientUnminimizedSpy.wait(100));
    QVERIFY(client->control->minimized);

    // Reopen the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The window shortcut should be set back to Ctrl+Alt+1.
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutRemember()
{
    QSKIP("KWin core doesn't try to save the last used window shortcut");

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::remember));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // If we press the window shortcut, the window should be brought back to user.
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // Change the window shortcut to Ctrl+Alt+2.
    win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_2}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // Reopen the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The window shortcut should be set to the last known value.
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_2}));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutForce()
{
    QSKIP("KWin core can't release forced window shortcuts");

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // If we press the window shortcut, the window should be brought back to user.
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // Any attempt to change the window shortcut should not succeed.
    win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(!clientUnminimizedSpy.wait(100));
    QVERIFY(client->control->minimized);

    // Reopen the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // The window shortcut should still be forced.
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutApplyNow()
{
    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->shortcut.isEmpty());

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::apply_now));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // The client should now have a window shortcut assigned.
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // Assign a different shortcut.
    win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_2}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // The rule should not be applied again.
    win::rules::evaluate_rules(client);
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_2}));

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testShortcutForceTemporarily()
{
    QSKIP("KWin core can't release forced window shortcuts");

    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("shortcut", "Ctrl+Alt+1");
    group.writeEntry("shortcutrule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);

    // If we press the window shortcut, the window should be brought back to user.
    QSignalSpy clientUnminimizedSpy(client->qobject.get(), &win::window_qobject::clientUnminimized);
    QVERIFY(clientUnminimizedSpy.isValid());
    quint32 timestamp = 1;
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(clientUnminimizedSpy.wait());
    QVERIFY(!client->control->minimized);

    // Any attempt to change the window shortcut should not succeed.
    win::set_shortcut(client, QStringLiteral("Ctrl+Alt+2"));
    QCOMPARE(client->control->shortcut, (QKeySequence{Qt::CTRL + Qt::ALT + Qt::Key_1}));
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized);
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_2, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    QVERIFY(!clientUnminimizedSpy.wait(100));
    QVERIFY(client->control->minimized);

    // The rule should be discarded when the client is closed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->shortcut.isEmpty());

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testDesktopFileDontAffect()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testDesktopFileApply()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testDesktopFileRemember()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testDesktopFileForce()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testDesktopFileApplyNow()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testDesktopFileForceTemporarily()
{
    // Currently, the desktop file name is derived from the app id. If the app id is
    // changed, then the old rules will be lost. Either setDesktopFileName should
    // be exposed or the desktop file name rule should be removed for wayland clients.
    QSKIP("Needs changes in KWin core to pass");
}

void TestXdgShellClientRules::testActiveOpacityDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityactive", 90);
    group.writeEntry("opacityactiverule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testActiveOpacityForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityactive", 90);
    group.writeEntry("opacityactiverule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testActiveOpacityForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityactive", 90);
    group.writeEntry("opacityactiverule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

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
    QVERIFY(Test::wait_for_destroyed(client));
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QCOMPARE(client->opacity(), 1.0);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testInactiveOpacityDontAffect()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityinactive", 80);
    group.writeEntry("opacityinactiverule", enum_index(win::rules::action::dont_affect));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);

    // Make the client inactive.
    win::set_active_window(*Test::app()->base.space, nullptr);
    QVERIFY(!client->control->active);

    // The opacity of the client should not be affected by the rule.
    QCOMPARE(client->opacity(), 1.0);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testInactiveOpacityForce()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityinactive", 80);
    group.writeEntry("opacityinactiverule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QCOMPARE(client->opacity(), 1.0);

    // Make the client inactive.
    win::set_active_window(*Test::app()->base.space, nullptr);
    QVERIFY(!client->control->active);

    // The opacity should be forced by the rule.
    QCOMPARE(client->opacity(), 0.8);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testInactiveOpacityForceTemporarily()
{
    // Initialize RuleBook with the test rule.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);
    KConfigGroup group = config->group("1");
    group.writeEntry("opacityinactive", 80);
    group.writeEntry("opacityinactiverule", enum_index(win::rules::action::force_temporarily));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();
    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // Create the test client.
    wayland_window* client;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<XdgShellToplevel> shellSurface;
    std::tie(client, surface, shellSurface) = createWindow("org.kde.foo");
    QVERIFY(client);
    QVERIFY(client->control->active);
    QCOMPARE(client->opacity(), 1.0);

    // Make the client inactive.
    win::set_active_window(*Test::app()->base.space, nullptr);
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
    win::set_active_window(*Test::app()->base.space, nullptr);
    QVERIFY(!client->control->active);
    QCOMPARE(client->opacity(), 1.0);

    // Destroy the client.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void TestXdgShellClientRules::testMatchAfterNameChange()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);

    KConfigGroup group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", enum_index(win::rules::action::force));
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
    group.sync();

    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    auto surface = Test::create_surface();
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);

    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active);
    QCOMPARE(c->control->keep_above, false);

    QSignalSpy desktopFileNameSpy(c->qobject.get(), &win::window_qobject::desktopFileNameChanged);
    QVERIFY(desktopFileNameSpy.isValid());

    shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
    QVERIFY(desktopFileNameSpy.wait());
    QCOMPARE(c->control->keep_above, true);
}

}

WAYLANDTEST_MAIN(KWin::TestXdgShellClientRules)
#include "xdgshellclient_rules_test.moc"
