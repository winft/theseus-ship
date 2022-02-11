/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2018 Martin Flöser <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License or (at your option) version 3 or any later version
accepted by the membership of KDE e.V. (or its successor approved
by the membership of KDE e.V.), which shall act as a proxy
defined in Section 14 of version 3 of the license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "base/x11/atoms.h"
#include "rules/rules.h"
#include "screens.h"
#include "win/controlling.h"
#include "win/move.h"
#include "win/stacking.h"
#include "win/virtual_desktops.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <Wrapland/Client/surface.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QUuid>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

namespace KWin
{

const QString s_destination{QStringLiteral("org.kde.KWin")};
const QString s_path{QStringLiteral("/KWin")};
const QString s_interface{QStringLiteral("org.kde.KWin")};

class TestDbusInterface : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testGetWindowInfoInvalidUuid();
    void testGetWindowInfoXdgShellClient();
    void testGetWindowInfoX11Client();
};

void TestDbusInterface::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    win::virtual_desktop_manager::self()->setCount(4);
}

void TestDbusInterface::init()
{
    Test::setup_wayland_connection();
}

void TestDbusInterface::cleanup()
{
    Test::destroy_wayland_connection();
}

namespace
{
QDBusPendingCall getWindowInfo(const QUuid& uuid)
{
    auto msg = QDBusMessage::createMethodCall(
        s_destination, s_path, s_interface, QStringLiteral("getWindowInfo"));
    msg.setArguments({uuid.toString()});
    return QDBusConnection::sessionBus().asyncCall(msg);
}
}

void TestDbusInterface::testGetWindowInfoInvalidUuid()
{
    QDBusPendingReply<QVariantMap> reply{getWindowInfo(QUuid::createUuid())};
    reply.waitForFinished();
    QVERIFY(reply.isValid());
    QVERIFY(!reply.isError());
    const auto windowData = reply.value();
    QVERIFY(windowData.empty());
}

void TestDbusInterface::testGetWindowInfoXdgShellClient()
{
    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
    shellSurface->setTitle(QStringLiteral("Test window"));

    // now let's render
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(clientAddedSpy.isEmpty());
    QVERIFY(clientAddedSpy.wait());
    auto client = clientAddedSpy.first().first().value<win::wayland::window*>();
    QVERIFY(client);

    // let's get the window info
    QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->internalId())};
    reply.waitForFinished();
    QVERIFY(reply.isValid());
    QVERIFY(!reply.isError());
    auto windowData = reply.value();
    QVERIFY(!windowData.isEmpty());
    QCOMPARE(windowData.size(), 24);
    QCOMPARE(windowData.value(QStringLiteral("type")).toInt(), NET::Normal);
    QCOMPARE(windowData.value(QStringLiteral("x")).toInt(), client->pos().x());
    QCOMPARE(windowData.value(QStringLiteral("y")).toInt(), client->pos().y());
    QCOMPARE(windowData.value(QStringLiteral("width")).toInt(), client->size().width());
    QCOMPARE(windowData.value(QStringLiteral("height")).toInt(), client->size().height());
    QCOMPARE(windowData.value(QStringLiteral("x11DesktopNumber")).toInt(), 1);
    QCOMPARE(windowData.value(QStringLiteral("minimized")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("fullscreen")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("keepAbove")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("keepBelow")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipTaskbar")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipPager")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipSwitcher")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("maximizeHorizontal")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("maximizeVertical")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("noBorder")).toBool(), true);
    QCOMPARE(windowData.value(QStringLiteral("clientMachine")).toString(), QString());
    QCOMPARE(windowData.value(QStringLiteral("localhost")).toBool(), true);
    QCOMPARE(windowData.value(QStringLiteral("role")).toString(), QString());
    QCOMPARE(windowData.value(QStringLiteral("resourceName")).toString(),
             QStringLiteral("testDbusInterface"));
    QCOMPARE(windowData.value(QStringLiteral("resourceClass")).toString(),
             QStringLiteral("org.kde.foo"));
    QCOMPARE(windowData.value(QStringLiteral("desktopFile")).toString(),
             QStringLiteral("org.kde.foo"));
    QCOMPARE(windowData.value(QStringLiteral("caption")).toString(), QStringLiteral("Test window"));

    auto verifyProperty = [client](const QString& name) {
        QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->internalId())};
        reply.waitForFinished();
        return reply.value().value(name).toBool();
    };

    QVERIFY(!client->control->minimized());
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized());
    QCOMPARE(verifyProperty(QStringLiteral("minimized")), true);

    QVERIFY(!client->control->keep_above());
    win::set_keep_above(client, true);
    QVERIFY(client->control->keep_above());
    QCOMPARE(verifyProperty(QStringLiteral("keepAbove")), true);

    QVERIFY(!client->control->keep_below());
    win::set_keep_below(client, true);
    QVERIFY(client->control->keep_below());
    QCOMPARE(verifyProperty(QStringLiteral("keepBelow")), true);

    QVERIFY(!client->control->skip_taskbar());
    win::set_skip_taskbar(client, true);
    QVERIFY(client->control->skip_taskbar());
    QCOMPARE(verifyProperty(QStringLiteral("skipTaskbar")), true);

    QVERIFY(!client->control->skip_pager());
    win::set_skip_pager(client, true);
    QVERIFY(client->control->skip_pager());
    QCOMPARE(verifyProperty(QStringLiteral("skipPager")), true);

    QVERIFY(!client->control->skip_switcher());
    win::set_skip_switcher(client, true);
    QVERIFY(client->control->skip_switcher());
    QCOMPARE(verifyProperty(QStringLiteral("skipSwitcher")), true);

    // not testing fullscreen, maximizeHorizontal, maximizeVertical and noBorder as those require
    // window geometry changes

    QCOMPARE(client->desktop(), 1);
    workspace()->sendClientToDesktop(client, 2, false);
    QCOMPARE(client->desktop(), 2);
    reply = getWindowInfo(client->internalId());
    reply.waitForFinished();
    QCOMPARE(reply.value().value(QStringLiteral("x11DesktopNumber")).toInt(), 2);

    win::move(client, QPoint(10, 20));
    reply = getWindowInfo(client->internalId());
    reply.waitForFinished();
    QCOMPARE(reply.value().value(QStringLiteral("x")).toInt(), client->pos().x());
    QCOMPARE(reply.value().value(QStringLiteral("y")).toInt(), client->pos().y());
    // not testing width, height as that would require window geometry change

    // finally close window
    const auto id = client->internalId();
    QSignalSpy windowClosedSpy(client, &win::wayland::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
    QCOMPARE(windowClosedSpy.count(), 1);

    reply = getWindowInfo(id);
    reply.waitForFinished();
    QVERIFY(reply.value().empty());
}

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

void TestDbusInterface::testGetWindowInfoX11Client()
{
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 600, 400);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    xcb_icccm_set_wm_class(c.get(), w, 7, "foo\0bar");
    NETWinInfo winInfo(c.get(), w, rootWindow(), NET::Properties(), NET::Properties2());
    winInfo.setName("Some caption");
    winInfo.setDesktopFileName("org.kde.foo");
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QCOMPARE(win::frame_to_client_size(client, client->size()), windowGeometry.size());

    // let's get the window info
    QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->internalId())};
    reply.waitForFinished();
    QVERIFY(reply.isValid());
    QVERIFY(!reply.isError());
    auto windowData = reply.value();
    QVERIFY(!windowData.isEmpty());
    QCOMPARE(windowData.size(), 24);
    QCOMPARE(windowData.value(QStringLiteral("type")).toInt(), NET::Normal);
    QCOMPARE(windowData.value(QStringLiteral("x")).toInt(), client->pos().x());
    QCOMPARE(windowData.value(QStringLiteral("y")).toInt(), client->pos().y());
    QCOMPARE(windowData.value(QStringLiteral("width")).toInt(), client->size().width());
    QCOMPARE(windowData.value(QStringLiteral("height")).toInt(), client->size().height());
    QCOMPARE(windowData.value(QStringLiteral("x11DesktopNumber")).toInt(), 1);
    QCOMPARE(windowData.value(QStringLiteral("minimized")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("shaded")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("fullscreen")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("keepAbove")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("keepBelow")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipTaskbar")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipPager")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("skipSwitcher")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("maximizeHorizontal")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("maximizeVertical")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("noBorder")).toBool(), false);
    QCOMPARE(windowData.value(QStringLiteral("role")).toString(), QString());
    QCOMPARE(windowData.value(QStringLiteral("resourceName")).toString(), QStringLiteral("foo"));
    QCOMPARE(windowData.value(QStringLiteral("resourceClass")).toString(), QStringLiteral("bar"));
    QCOMPARE(windowData.value(QStringLiteral("desktopFile")).toString(),
             QStringLiteral("org.kde.foo"));
    QCOMPARE(windowData.value(QStringLiteral("caption")).toString(),
             QStringLiteral("Some caption"));
    // not testing clientmachine as that is system dependent
    // due to that also not testing localhost

    auto verifyProperty = [client](const QString& name) {
        QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->internalId())};
        reply.waitForFinished();
        return reply.value().value(name).toBool();
    };

    QVERIFY(!client->control->minimized());
    win::set_minimized(client, true);
    QVERIFY(client->control->minimized());
    QCOMPARE(verifyProperty(QStringLiteral("minimized")), true);

    QVERIFY(!client->control->keep_above());
    win::set_keep_above(client, true);
    QVERIFY(client->control->keep_above());
    QCOMPARE(verifyProperty(QStringLiteral("keepAbove")), true);

    QVERIFY(!client->control->keep_below());
    win::set_keep_below(client, true);
    QVERIFY(client->control->keep_below());
    QCOMPARE(verifyProperty(QStringLiteral("keepBelow")), true);

    QVERIFY(!client->control->skip_taskbar());
    win::set_skip_taskbar(client, true);
    QVERIFY(client->control->skip_taskbar());
    QCOMPARE(verifyProperty(QStringLiteral("skipTaskbar")), true);

    QVERIFY(!client->control->skip_pager());
    win::set_skip_pager(client, true);
    QVERIFY(client->control->skip_pager());
    QCOMPARE(verifyProperty(QStringLiteral("skipPager")), true);

    QVERIFY(!client->control->skip_switcher());
    win::set_skip_switcher(client, true);
    QVERIFY(client->control->skip_switcher());
    QCOMPARE(verifyProperty(QStringLiteral("skipSwitcher")), true);

    QVERIFY(!client->noBorder());
    client->setNoBorder(true);
    QVERIFY(client->noBorder());
    QCOMPARE(verifyProperty(QStringLiteral("noBorder")), true);
    client->setNoBorder(false);
    QVERIFY(!client->noBorder());

    QVERIFY(!client->control->fullscreen());
    client->setFullScreen(true);
    QVERIFY(client->control->fullscreen());
    QVERIFY(win::frame_to_client_size(client, client->size()) != windowGeometry.size());
    QCOMPARE(verifyProperty(QStringLiteral("fullscreen")), true);
    reply = getWindowInfo(client->internalId());
    reply.waitForFinished();
    QCOMPARE(reply.value().value(QStringLiteral("width")).toInt(), client->size().width());
    QCOMPARE(reply.value().value(QStringLiteral("height")).toInt(), client->size().height());

    client->setFullScreen(false);
    QVERIFY(!client->control->fullscreen());
    QCOMPARE(verifyProperty(QStringLiteral("fullscreen")), false);

    // maximize
    win::set_maximize(client, true, false);
    QCOMPARE(verifyProperty(QStringLiteral("maximizeVertical")), true);
    QCOMPARE(verifyProperty(QStringLiteral("maximizeHorizontal")), false);
    win::set_maximize(client, false, true);
    QCOMPARE(verifyProperty(QStringLiteral("maximizeVertical")), false);
    QCOMPARE(verifyProperty(QStringLiteral("maximizeHorizontal")), true);

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());

    const auto id = client->internalId();

    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());

    QVERIFY(!windowClosedSpy.count());

    QVERIFY(windowClosedSpy.wait());
    c.reset();

    reply = getWindowInfo(id);
    reply.waitForFinished();
    QVERIFY(reply.value().empty());
}

}

WAYLANDTEST_MAIN(KWin::TestDbusInterface)
#include "dbus_interface_test.moc"
