/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "screens.h"
#include "useractions.h"
#include "win/wayland/window.h"
#include "workspace.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin
{

class TestDontCrashUseractionsMenu : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testShowHideShowUseractionsMenu();
};

void TestDontCrashUseractionsMenu::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // force style to breeze as that's the one which triggered the crash
    QVERIFY(kwinApp()->setStyle(QStringLiteral("breeze")));

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    QCOMPARE(Test::app()->base.screens.count(), 2);
    QCOMPARE(Test::app()->base.screens.geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(Test::app()->base.screens.geometry(1), QRect(1280, 0, 1280, 1024));
}

void TestDontCrashUseractionsMenu::init()
{
    Test::setup_wayland_connection();

    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(1280, 512));
}

void TestDontCrashUseractionsMenu::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestDontCrashUseractionsMenu::testShowHideShowUseractionsMenu()
{
    // this test creates the condition of BUG 382063
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    workspace()->showWindowMenu(QRect(), client);
    auto userActionsMenu = workspace()->userActionsMenu();
    QTRY_VERIFY(userActionsMenu->isShown());
    QVERIFY(userActionsMenu->hasClient());

    Test::keyboard_key_pressed(KEY_ESC, 0);
    Test::keyboard_key_released(KEY_ESC, 1);
    QTRY_VERIFY(!userActionsMenu->isShown());
    QVERIFY(!userActionsMenu->hasClient());

    // and show again, this triggers BUG 382063
    workspace()->showWindowMenu(QRect(), client);
    QTRY_VERIFY(userActionsMenu->isShown());
    QVERIFY(userActionsMenu->hasClient());
}

}

WAYLANDTEST_MAIN(KWin::TestDontCrashUseractionsMenu)
#include "dont_crash_useractions_menu.moc"
