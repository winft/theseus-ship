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
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"

#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

#include <QSocketNotifier>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class XWaylandInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void testPointerEnterLeave();
};

void XWaylandInputTest::initTestCase()
{
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void XWaylandInputTest::init()
{
    input::get_cursor()->set_pos(QPoint(640, 512));
    QVERIFY(Test::app()->workspace->m_windows.empty());
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

class X11EventReaderHelper : public QObject
{
    Q_OBJECT
public:
    X11EventReaderHelper(xcb_connection_t* c);

Q_SIGNALS:
    void entered();
    void left();

private:
    void processXcbEvents();
    xcb_connection_t* m_connection;
    QSocketNotifier* m_notifier;
};

X11EventReaderHelper::X11EventReaderHelper(xcb_connection_t* c)
    : QObject()
    , m_connection(c)
    , m_notifier(
          new QSocketNotifier(xcb_get_file_descriptor(m_connection), QSocketNotifier::Read, this))
{
    connect(m_notifier, &QSocketNotifier::activated, this, &X11EventReaderHelper::processXcbEvents);
    connect(QCoreApplication::eventDispatcher(),
            &QAbstractEventDispatcher::aboutToBlock,
            this,
            &X11EventReaderHelper::processXcbEvents);
    connect(QCoreApplication::eventDispatcher(),
            &QAbstractEventDispatcher::awake,
            this,
            &X11EventReaderHelper::processXcbEvents);
}

void X11EventReaderHelper::processXcbEvents()
{
    while (auto event = xcb_poll_for_event(m_connection)) {
        const uint8_t eventType = event->response_type & ~0x80;
        switch (eventType) {
        case XCB_ENTER_NOTIFY:
            Q_EMIT entered();
            break;
        case XCB_LEAVE_NOTIFY:
            Q_EMIT left();
            break;
        }
        free(event);
    }
    xcb_flush(m_connection);
}

void XWaylandInputTest::testPointerEnterLeave()
{
    // this test simulates a pointer enter and pointer leave on an X11 window

    // create the test window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    if (xcb_get_setup(c.get())->release_number < 11800000) {
        QSKIP("XWayland 1.18 required");
    }
    X11EventReaderHelper eventReader(c.get());
    QSignalSpy enteredSpy(&eventReader, &X11EventReaderHelper::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(&eventReader, &X11EventReaderHelper::left);
    QVERIFY(leftSpy.isValid());
    // atom for the screenedge show hide functionality
    base::x11::xcb::atom atom(QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 100, 200);
    const uint32_t values[] = {XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
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
                      XCB_CW_EVENT_MASK,
                      values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->workspace.get(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client);
    QVERIFY(win::decoration(client));
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());
    QVERIFY(!client->ready_for_painting);
    QMetaObject::invokeMethod(client, "setReadyForPainting");
    QVERIFY(client->ready_for_painting);
    QVERIFY(!client->surface);
    QSignalSpy surfaceChangedSpy(client, &Toplevel::surfaceChanged);
    QVERIFY(surfaceChangedSpy.isValid());
    QVERIFY(surfaceChangedSpy.wait());
    QVERIFY(client->surface);

    // move pointer into the window, should trigger an enter
    QVERIFY(!client->frameGeometry().contains(input::get_cursor()->pos()));
    QVERIFY(enteredSpy.isEmpty());
    input::get_cursor()->set_pos(client->frameGeometry().center());
    QCOMPARE(waylandServer()->seat()->pointers().get_focus().surface, client->surface);
    QVERIFY(!waylandServer()->seat()->pointers().get_focus().devices.empty());
    QVERIFY(enteredSpy.wait());

    // move out of window
    input::get_cursor()->set_pos(client->frameGeometry().bottomRight() + QPoint(10, 10));
    QVERIFY(leftSpy.wait());

    // destroy window again
    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::XWaylandInputTest)
#include "xwayland_input_test.moc"
