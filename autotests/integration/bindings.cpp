/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "script/platform.h"
#include "script/script.h"
#include "win/control.h"
#include "win/move.h"
#include "win/wayland/window.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("bindings", "[input],[win]")
{
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("bindings", operation_mode);
    setup.start();
    setup_wayland_connection();

    cursor()->set_pos(QPoint(640, 512));
    QCOMPARE(cursor()->pos(), QPoint(640, 512));

    SECTION("switch window")
    {
        // first create windows
        auto surface1 = create_surface();
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        auto c1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        auto surface2 = create_surface();
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        auto c2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        auto surface3 = create_surface();
        auto shellSurface3 = create_xdg_shell_toplevel(surface3);
        auto c3 = render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        auto surface4 = create_surface();
        auto shellSurface4 = create_xdg_shell_toplevel(surface4);
        auto c4 = render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

        QVERIFY(c4->control->active);
        QVERIFY(c4 != c3);
        QVERIFY(c3 != c2);
        QVERIFY(c2 != c1);

        // let's position all windows
        win::move(c1, QPoint(0, 0));
        win::move(c2, QPoint(200, 0));
        win::move(c3, QPoint(200, 200));
        win::move(c4, QPoint(0, 200));

        QCOMPARE(c1->geo.pos(), QPoint(0, 0));
        QCOMPARE(c2->geo.pos(), QPoint(200, 0));
        QCOMPARE(c3->geo.pos(), QPoint(200, 200));
        QCOMPARE(c4->geo.pos(), QPoint(0, 200));

        // now let's trigger the shortcuts

        // invoke global shortcut through dbus
        auto invokeShortcut = [](const QString& shortcut) {
            auto msg
                = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                 QStringLiteral("/component/kwin"),
                                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                                 QStringLiteral("invokeShortcut"));
            msg.setArguments(QList<QVariant>{shortcut});
            QDBusConnection::sessionBus().asyncCall(msg);
        };
        invokeShortcut(QStringLiteral("Switch Window Up"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c1);
        invokeShortcut(QStringLiteral("Switch Window Right"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c2);
        invokeShortcut(QStringLiteral("Switch Window Down"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c3);
        invokeShortcut(QStringLiteral("Switch Window Left"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c4);
        // test opposite direction
        invokeShortcut(QStringLiteral("Switch Window Left"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c3);
        invokeShortcut(QStringLiteral("Switch Window Down"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c2);
        invokeShortcut(QStringLiteral("Switch Window Right"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c1);
        invokeShortcut(QStringLiteral("Switch Window Up"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c4);
    }

    SECTION("switch window script")
    {
        QVERIFY(setup.base->script);

        // first create windows
        auto surface1 = create_surface();
        auto shellSurface1 = create_xdg_shell_toplevel(surface1);
        auto c1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        auto surface2 = create_surface();
        auto shellSurface2 = create_xdg_shell_toplevel(surface2);
        auto c2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        auto surface3 = create_surface();
        auto shellSurface3 = create_xdg_shell_toplevel(surface3);
        auto c3 = render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        auto surface4 = create_surface();
        auto shellSurface4 = create_xdg_shell_toplevel(surface4);
        auto c4 = render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

        QVERIFY(c4->control->active);
        QVERIFY(c4 != c3);
        QVERIFY(c3 != c2);
        QVERIFY(c2 != c1);

        // let's position all windows
        win::move(c1, QPoint(0, 0));
        win::move(c2, QPoint(200, 0));
        win::move(c3, QPoint(200, 200));
        win::move(c4, QPoint(0, 200));

        auto runScript = [&](auto const& slot) {
            QTemporaryFile tmpFile;
            QVERIFY(tmpFile.open());
            QTextStream out(&tmpFile);
            out << "workspace." << slot << "()";
            out.flush();

            auto const id = setup.base->script->loadScript(tmpFile.fileName());
            QVERIFY(id != -1);
            QVERIFY(setup.base->script->isScriptLoaded(tmpFile.fileName()));
            auto s = setup.base->script->findScript(tmpFile.fileName());
            QVERIFY(s);
            QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
            QVERIFY(runningChangedSpy.isValid());
            s->run();
            QTRY_COMPARE(runningChangedSpy.count(), 1);
        };

        runScript(QStringLiteral("slotSwitchWindowUp"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c1);
        runScript(QStringLiteral("slotSwitchWindowRight"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c2);
        runScript(QStringLiteral("slotSwitchWindowDown"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c3);
        runScript(QStringLiteral("slotSwitchWindowLeft"));
        QTRY_COMPARE(get_wayland_window(setup.base->space->stacking.active), c4);
    }

    SECTION("switch window script")
    {
        auto subspace = GENERATE(range(2, 20));

        // first go to subspace one
        auto& vd_manager = setup.base->space->subspace_manager;
        vd_manager->setCurrent(vd_manager->subspaces().first());

        // now create a window
        auto surface = create_surface();
        auto shellSurface = create_xdg_shell_toplevel(surface);

        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QSignalSpy subspacesChangedSpy(c->qobject.get(), &win::window_qobject::subspaces_changed);
        QVERIFY(subspacesChangedSpy.isValid());

        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);

        vd_manager->setCount(subspace);

        // now trigger the shortcut
        auto invokeShortcut = [](int subspace) {
            auto msg
                = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                 QStringLiteral("/component/kwin"),
                                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                                 QStringLiteral("invokeShortcut"));
            msg.setArguments(QList<QVariant>{QStringLiteral("Window to Desktop %1").arg(subspace)});
            QDBusConnection::sessionBus().asyncCall(msg);
        };

        invokeShortcut(subspace);
        QVERIFY(subspacesChangedSpy.wait());
        QCOMPARE(win::get_subspace(*c), subspace);

        // back to subspace 1
        invokeShortcut(1);
        QVERIFY(subspacesChangedSpy.wait());
        QCOMPARE(win::get_subspace(*c), 1);

        // invoke with one subspace too many
        invokeShortcut(subspace + 1);
        // that should fail
        QVERIFY(!subspacesChangedSpy.wait(100));
    }
}

}
