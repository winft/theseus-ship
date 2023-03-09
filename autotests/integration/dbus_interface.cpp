/*
SPDX-FileCopyrightText: 2018 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "base/x11/atoms.h"
#include "win/actions.h"
#include "win/controlling.h"
#include "win/desktop_space.h"
#include "win/move.h"
#include "win/space.h"
#include "win/virtual_desktops.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"

#include <Wrapland/Client/surface.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QUuid>

#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

const QString s_destination{QStringLiteral("org.kde.KWin")};
const QString s_path{QStringLiteral("/KWin")};
const QString s_interface{QStringLiteral("org.kde.KWin")};

namespace
{

QDBusPendingCall getWindowInfo(const QUuid& uuid)
{
    auto msg = QDBusMessage::createMethodCall(
        s_destination, s_path, s_interface, QStringLiteral("getWindowInfo"));
    msg.setArguments({uuid.toString()});
    return QDBusConnection::sessionBus().asyncCall(msg);
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

}

TEST_CASE("dbus interface", "[base]")
{
    test::setup setup("dbus-interface", base::operation_mode::xwayland);
    setup.start();
    setup.base->space->virtual_desktop_manager->setCount(4);

    setup_wayland_connection();

    SECTION("get window info with invalid uuid")
    {
        QDBusPendingReply<QVariantMap> reply{getWindowInfo(QUuid::createUuid())};
        reply.waitForFinished();
        QVERIFY(reply.isValid());
        QVERIFY(!reply.isError());
        const auto windowData = reply.value();
        QVERIFY(windowData.empty());
    }

    SECTION("get window info for xdg-shell client")
    {
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
        shellSurface->setTitle(QStringLiteral("Test window"));

        // now let's render
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(clientAddedSpy.isEmpty());
        QVERIFY(clientAddedSpy.wait());

        auto client_id = clientAddedSpy.first().first().value<quint32>();
        auto client = get_wayland_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);

        // let's get the window info
        QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->meta.internal_id)};
        reply.waitForFinished();
        QVERIFY(reply.isValid());
        QVERIFY(!reply.isError());
        auto windowData = reply.value();
        QVERIFY(!windowData.isEmpty());
        QCOMPARE(windowData.size(), 25);
        QCOMPARE(windowData.value(QStringLiteral("type")).toInt(),
                 static_cast<int>(win::win_type::normal));
        QCOMPARE(windowData.value(QStringLiteral("x")).toInt(), client->geo.pos().x());
        QCOMPARE(windowData.value(QStringLiteral("y")).toInt(), client->geo.pos().y());
        QCOMPARE(windowData.value(QStringLiteral("width")).toInt(), client->geo.size().width());
        QCOMPARE(windowData.value(QStringLiteral("height")).toInt(), client->geo.size().height());
        QCOMPARE(windowData.value(QStringLiteral("desktops")), win::desktop_ids(client));
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
                 QStringLiteral("tests"));
        QCOMPARE(windowData.value(QStringLiteral("resourceClass")).toString(),
                 QStringLiteral("org.kde.foo"));
        QCOMPARE(windowData.value(QStringLiteral("desktopFile")).toString(),
                 QStringLiteral("org.kde.foo"));
        QCOMPARE(windowData.value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Test window"));

        auto verifyProperty = [client](const QString& name) {
            QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->meta.internal_id)};
            reply.waitForFinished();
            return reply.value().value(name).toBool();
        };

        QVERIFY(!client->control->minimized);
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        QCOMPARE(verifyProperty(QStringLiteral("minimized")), true);

        QVERIFY(!client->control->keep_above);
        win::set_keep_above(client, true);
        QVERIFY(client->control->keep_above);
        QCOMPARE(verifyProperty(QStringLiteral("keepAbove")), true);

        QVERIFY(!client->control->keep_below);
        win::set_keep_below(client, true);
        QVERIFY(client->control->keep_below);
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

        // not testing fullscreen, maximizeHorizontal, maximizeVertical and noBorder as those
        // require window geometry changes

        QCOMPARE(win::get_desktop(*client), 1);
        win::send_window_to_desktop(*setup.base->space, client, 2, false);
        QCOMPARE(win::get_desktop(*client), 2);
        reply = getWindowInfo(client->meta.internal_id);
        reply.waitForFinished();
        QCOMPARE(reply.value().value(QStringLiteral("desktops")).toStringList(),
                 win::desktop_ids(client));

        win::move(client, QPoint(10, 20));
        reply = getWindowInfo(client->meta.internal_id);
        reply.waitForFinished();
        QCOMPARE(reply.value().value(QStringLiteral("x")).toInt(), client->geo.pos().x());
        QCOMPARE(reply.value().value(QStringLiteral("y")).toInt(), client->geo.pos().y());
        // not testing width, height as that would require window geometry change

        // finally close window
        const auto id = client->meta.internal_id;
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        shellSurface.reset();
        surface.reset();
        QVERIFY(windowClosedSpy.wait());
        QCOMPARE(windowClosedSpy.count(), 1);

        reply = getWindowInfo(id);
        reply.waitForFinished();
        QVERIFY(reply.value().empty());
    }

    SECTION("get window info for x11 client")
    {
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 600, 400);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
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
        win::x11::net::win_info winInfo(c.get(),
                                        w,
                                        setup.base->x11_data.root_window,
                                        win::x11::net::Properties(),
                                        win::x11::net::Properties2());
        winInfo.setName("Some caption");
        winInfo.setDesktopFileName("org.kde.foo");
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &win::space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(win::frame_to_client_size(client, client->geo.size()), windowGeometry.size());

        // let's get the window info
        QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->meta.internal_id)};
        reply.waitForFinished();
        QVERIFY(reply.isValid());
        QVERIFY(!reply.isError());
        auto windowData = reply.value();
        QVERIFY(!windowData.isEmpty());
        QCOMPARE(windowData.size(), 25);
        QCOMPARE(windowData.value(QStringLiteral("type")).toInt(),
                 static_cast<int>(win::win_type::normal));
        QCOMPARE(windowData.value(QStringLiteral("x")).toInt(), client->geo.pos().x());
        QCOMPARE(windowData.value(QStringLiteral("y")).toInt(), client->geo.pos().y());
        QCOMPARE(windowData.value(QStringLiteral("width")).toInt(), client->geo.size().width());
        QCOMPARE(windowData.value(QStringLiteral("height")).toInt(), client->geo.size().height());
        QCOMPARE(windowData.value(QStringLiteral("desktops")), win::desktop_ids(client));
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
        QCOMPARE(windowData.value(QStringLiteral("resourceName")).toString(),
                 QStringLiteral("foo"));
        QCOMPARE(windowData.value(QStringLiteral("resourceClass")).toString(),
                 QStringLiteral("bar"));
        QCOMPARE(windowData.value(QStringLiteral("desktopFile")).toString(),
                 QStringLiteral("org.kde.foo"));
        QCOMPARE(windowData.value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Some caption"));
        // not testing clientmachine as that is system dependent
        // due to that also not testing localhost

        auto verifyProperty = [client](const QString& name) {
            QDBusPendingReply<QVariantMap> reply{getWindowInfo(client->meta.internal_id)};
            reply.waitForFinished();
            return reply.value().value(name).toBool();
        };

        QVERIFY(!client->control->minimized);
        win::set_minimized(client, true);
        QVERIFY(client->control->minimized);
        QCOMPARE(verifyProperty(QStringLiteral("minimized")), true);

        QVERIFY(!client->control->keep_above);
        win::set_keep_above(client, true);
        QVERIFY(client->control->keep_above);
        QCOMPARE(verifyProperty(QStringLiteral("keepAbove")), true);

        QVERIFY(!client->control->keep_below);
        win::set_keep_below(client, true);
        QVERIFY(client->control->keep_below);
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

        QVERIFY(!client->control->fullscreen);
        client->setFullScreen(true);
        QVERIFY(client->control->fullscreen);
        QVERIFY(win::frame_to_client_size(client, client->geo.size()) != windowGeometry.size());
        QCOMPARE(verifyProperty(QStringLiteral("fullscreen")), true);
        reply = getWindowInfo(client->meta.internal_id);
        reply.waitForFinished();
        QCOMPARE(reply.value().value(QStringLiteral("width")).toInt(), client->geo.size().width());
        QCOMPARE(reply.value().value(QStringLiteral("height")).toInt(),
                 client->geo.size().height());

        client->setFullScreen(false);
        QVERIFY(!client->control->fullscreen);
        QCOMPARE(verifyProperty(QStringLiteral("fullscreen")), false);

        // maximize
        win::set_maximize(client, true, false);
        QCOMPARE(verifyProperty(QStringLiteral("maximizeVertical")), true);
        QCOMPARE(verifyProperty(QStringLiteral("maximizeHorizontal")), false);
        win::set_maximize(client, false, true);
        QCOMPARE(verifyProperty(QStringLiteral("maximizeVertical")), false);
        QCOMPARE(verifyProperty(QStringLiteral("maximizeHorizontal")), true);

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        const auto id = client->meta.internal_id;

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

}
