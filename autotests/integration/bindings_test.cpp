/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/control.h"
#include "win/move.h"
#include "win/space.h"
#include "win/virtual_desktops.h"
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
    test::setup setup("bindings");
    setup.start();
    Test::setup_wayland_connection();

    Test::cursor()->set_pos(QPoint(640, 512));
    QCOMPARE(Test::cursor()->pos(), QPoint(640, 512));

    SECTION("switch window")
    {
        // first create windows
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto c4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

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
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c1);
        invokeShortcut(QStringLiteral("Switch Window Right"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c2);
        invokeShortcut(QStringLiteral("Switch Window Down"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c3);
        invokeShortcut(QStringLiteral("Switch Window Left"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c4);
        // test opposite direction
        invokeShortcut(QStringLiteral("Switch Window Left"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c3);
        invokeShortcut(QStringLiteral("Switch Window Down"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c2);
        invokeShortcut(QStringLiteral("Switch Window Right"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c1);
        invokeShortcut(QStringLiteral("Switch Window Up"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c4);
    }

    SECTION("switch window script")
    {
        QVERIFY(setup.base->space->scripting);

        // first create windows
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto c4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

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

            auto const id = setup.base->space->scripting->loadScript(tmpFile.fileName());
            QVERIFY(id != -1);
            QVERIFY(setup.base->space->scripting->isScriptLoaded(tmpFile.fileName()));
            auto s = setup.base->space->scripting->findScript(tmpFile.fileName());
            QVERIFY(s);
            QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
            QVERIFY(runningChangedSpy.isValid());
            s->run();
            QTRY_COMPARE(runningChangedSpy.count(), 1);
        };

        runScript(QStringLiteral("slotSwitchWindowUp"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c1);
        runScript(QStringLiteral("slotSwitchWindowRight"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c2);
        runScript(QStringLiteral("slotSwitchWindowDown"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c3);
        runScript(QStringLiteral("slotSwitchWindowLeft"));
        QTRY_COMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c4);
    }

    SECTION("switch window script")
    {
        auto desktop = GENERATE(range(2, 20));

        // first go to desktop one
        auto& vd_manager = setup.base->space->virtual_desktop_manager;
        vd_manager->setCurrent(vd_manager->desktops().first());

        // now create a window
        auto surface = Test::create_surface();
        auto shellSurface = Test::create_xdg_shell_toplevel(surface);

        auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QSignalSpy desktopChangedSpy(c->qobject.get(), &win::window_qobject::desktopChanged);
        QVERIFY(desktopChangedSpy.isValid());

        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c);

        vd_manager->setCount(desktop);

        // now trigger the shortcut
        auto invokeShortcut = [](int desktop) {
            auto msg
                = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                 QStringLiteral("/component/kwin"),
                                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                                 QStringLiteral("invokeShortcut"));
            msg.setArguments(QList<QVariant>{QStringLiteral("Window to Desktop %1").arg(desktop)});
            QDBusConnection::sessionBus().asyncCall(msg);
        };

        invokeShortcut(desktop);
        QVERIFY(desktopChangedSpy.wait());
        QCOMPARE(win::get_desktop(*c), desktop);

        // back to desktop 1
        invokeShortcut(1);
        QVERIFY(desktopChangedSpy.wait());
        QCOMPARE(win::get_desktop(*c), 1);

        // invoke with one desktop too many
        invokeShortcut(desktop + 1);
        // that should fail
        QVERIFY(!desktopChangedSpy.wait(100));
    }
}

}
