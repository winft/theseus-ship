/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "activities.h"
#include "input/cursor.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "screenedge.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"
#include <kwineffects.h>

#include "win/x11/activity.h"
#include "win/x11/window.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class ActivitiesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void testSetOnActivitiesValidates();

private:
};

void ActivitiesTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));

    kwinApp()->setUseKActivities(true);
    kwinApp()->start();
    QMetaObject::invokeMethod(
        kwinApp()->platform, "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    QVERIFY(startup_spy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
}

void ActivitiesTest::cleanupTestCase()
{
    // terminate any still running kactivitymanagerd
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.ActivityManager"),
                                       QStringLiteral("/ActivityManager"),
                                       QStringLiteral("org.qtproject.Qt.QCoreApplication"),
                                       QStringLiteral("quit")));
}

void ActivitiesTest::init()
{
    screens()->setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void ActivitiesTest::cleanup()
{
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

void ActivitiesTest::testSetOnActivitiesValidates()
{
    // this test creates a Client and sets it on activities which don't exist
    // that should result in the window being on all activities
    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry(0, 0, 100, 200);

    auto cookie = xcb_create_window_checked(c.get(),
                                            0,
                                            w,
                                            rootWindow(),
                                            windowGeometry.x(),
                                            windowGeometry.y(),
                                            windowGeometry.width(),
                                            windowGeometry.height(),
                                            0,
                                            XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                            0,
                                            0,
                                            nullptr);
    QVERIFY(!xcb_request_check(c.get(), cookie));
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(win::decoration(client) != nullptr);

    // verify the test machine doesn't have the following activities used
    QVERIFY(!Activities::self()->all().contains(QStringLiteral("foo")));
    QVERIFY(!Activities::self()->all().contains(QStringLiteral("bar")));

    // setting the client to an invalid activities should result in the client being on all
    // activities
    win::x11::set_on_activity(client, QStringLiteral("foo"), true);
    QVERIFY(client->isOnAllActivities());
    QVERIFY(!client->activities().contains(QLatin1String("foo")));

    client->setOnActivities(QStringList{QStringLiteral("foo"), QStringLiteral("bar")});
    QVERIFY(client->isOnAllActivities());
    QVERIFY(!client->activities().contains(QLatin1String("foo")));
    QVERIFY(!client->activities().contains(QLatin1String("bar")));

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::ActivitiesTest)
#include "activities_test.moc"
