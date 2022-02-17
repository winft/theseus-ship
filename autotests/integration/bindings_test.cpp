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
#include "screens.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/control.h"
#include "win/move.h"
#include "win/space.h"
#include "win/virtual_desktops.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>

using namespace Wrapland::Client;

namespace KWin
{

class BindingsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSwitchWindow();
    void testSwitchWindowScript();
    void testWindowToDesktop_data();
    void testWindowToDesktop();
};

void BindingsTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void BindingsTest::init()
{
    Test::setup_wayland_connection();
    input::get_cursor()->set_pos(QPoint(640, 512));
    QCOMPARE(input::get_cursor()->pos(), QPoint(640, 512));
}

void BindingsTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void BindingsTest::testSwitchWindow()
{
    // first create windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto c4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

    QVERIFY(c4->control->active());
    QVERIFY(c4 != c3);
    QVERIFY(c3 != c2);
    QVERIFY(c2 != c1);

    // let's position all windows
    win::move(c1, QPoint(0, 0));
    win::move(c2, QPoint(200, 0));
    win::move(c3, QPoint(200, 200));
    win::move(c4, QPoint(0, 200));

    QCOMPARE(c1->pos(), QPoint(0, 0));
    QCOMPARE(c2->pos(), QPoint(200, 0));
    QCOMPARE(c3->pos(), QPoint(200, 200));
    QCOMPARE(c4->pos(), QPoint(0, 200));

    // now let's trigger the shortcuts

    // invoke global shortcut through dbus
    auto invokeShortcut = [](const QString& shortcut) {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                  QStringLiteral("/component/kwin"),
                                                  QStringLiteral("org.kde.kglobalaccel.Component"),
                                                  QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{shortcut});
        QDBusConnection::sessionBus().asyncCall(msg);
    };
    invokeShortcut(QStringLiteral("Switch Window Up"));
    QTRY_COMPARE(workspace()->activeClient(), c1);
    invokeShortcut(QStringLiteral("Switch Window Right"));
    QTRY_COMPARE(workspace()->activeClient(), c2);
    invokeShortcut(QStringLiteral("Switch Window Down"));
    QTRY_COMPARE(workspace()->activeClient(), c3);
    invokeShortcut(QStringLiteral("Switch Window Left"));
    QTRY_COMPARE(workspace()->activeClient(), c4);
    // test opposite direction
    invokeShortcut(QStringLiteral("Switch Window Left"));
    QTRY_COMPARE(workspace()->activeClient(), c3);
    invokeShortcut(QStringLiteral("Switch Window Down"));
    QTRY_COMPARE(workspace()->activeClient(), c2);
    invokeShortcut(QStringLiteral("Switch Window Right"));
    QTRY_COMPARE(workspace()->activeClient(), c1);
    invokeShortcut(QStringLiteral("Switch Window Up"));
    QTRY_COMPARE(workspace()->activeClient(), c4);
}

void BindingsTest::testSwitchWindowScript()
{
    QVERIFY(workspace()->scripting);

    // first create windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
    std::unique_ptr<Surface> surface4(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    auto c4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);

    QVERIFY(c4->control->active());
    QVERIFY(c4 != c3);
    QVERIFY(c3 != c2);
    QVERIFY(c2 != c1);

    // let's position all windows
    win::move(c1, QPoint(0, 0));
    win::move(c2, QPoint(200, 0));
    win::move(c3, QPoint(200, 200));
    win::move(c4, QPoint(0, 200));

    auto runScript = [](const QString& slot) {
        QTemporaryFile tmpFile;
        QVERIFY(tmpFile.open());
        QTextStream out(&tmpFile);
        out << "workspace." << slot << "()";
        out.flush();

        auto const id = workspace()->scripting->loadScript(tmpFile.fileName());
        QVERIFY(id != -1);
        QVERIFY(workspace()->scripting->isScriptLoaded(tmpFile.fileName()));
        auto s = workspace()->scripting->findScript(tmpFile.fileName());
        QVERIFY(s);
        QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
        QVERIFY(runningChangedSpy.isValid());
        s->run();
        QTRY_COMPARE(runningChangedSpy.count(), 1);
    };

    runScript(QStringLiteral("slotSwitchWindowUp"));
    QTRY_COMPARE(workspace()->activeClient(), c1);
    runScript(QStringLiteral("slotSwitchWindowRight"));
    QTRY_COMPARE(workspace()->activeClient(), c2);
    runScript(QStringLiteral("slotSwitchWindowDown"));
    QTRY_COMPARE(workspace()->activeClient(), c3);
    runScript(QStringLiteral("slotSwitchWindowLeft"));
    QTRY_COMPARE(workspace()->activeClient(), c4);
}

void BindingsTest::testWindowToDesktop_data()
{
    QTest::addColumn<int>("desktop");

    QTest::newRow("2") << 2;
    QTest::newRow("3") << 3;
    QTest::newRow("4") << 4;
    QTest::newRow("5") << 5;
    QTest::newRow("6") << 6;
    QTest::newRow("7") << 7;
    QTest::newRow("8") << 8;
    QTest::newRow("9") << 9;
    QTest::newRow("10") << 10;
    QTest::newRow("11") << 11;
    QTest::newRow("12") << 12;
    QTest::newRow("13") << 13;
    QTest::newRow("14") << 14;
    QTest::newRow("15") << 15;
    QTest::newRow("16") << 16;
    QTest::newRow("17") << 17;
    QTest::newRow("18") << 18;
    QTest::newRow("19") << 19;
    QTest::newRow("20") << 20;
}

void BindingsTest::testWindowToDesktop()
{
    // first go to desktop one
    win::virtual_desktop_manager::self()->setCurrent(
        win::virtual_desktop_manager::self()->desktops().first());

    // now create a window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QSignalSpy desktopChangedSpy(c, &Toplevel::desktopChanged);
    QVERIFY(desktopChangedSpy.isValid());
    QCOMPARE(workspace()->activeClient(), c);

    QFETCH(int, desktop);
    win::virtual_desktop_manager::self()->setCount(desktop);

    // now trigger the shortcut
    auto invokeShortcut = [](int desktop) {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                  QStringLiteral("/component/kwin"),
                                                  QStringLiteral("org.kde.kglobalaccel.Component"),
                                                  QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{QStringLiteral("Window to Desktop %1").arg(desktop)});
        QDBusConnection::sessionBus().asyncCall(msg);
    };
    invokeShortcut(desktop);
    QVERIFY(desktopChangedSpy.wait());
    QCOMPARE(c->desktop(), desktop);
    // back to desktop 1
    invokeShortcut(1);
    QVERIFY(desktopChangedSpy.wait());
    QCOMPARE(c->desktop(), 1);
    // invoke with one desktop too many
    invokeShortcut(desktop + 1);
    // that should fail
    QVERIFY(!desktopChangedSpy.wait(100));
}

}

WAYLANDTEST_MAIN(KWin::BindingsTest)
#include "bindings_test.moc"
