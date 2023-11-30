/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <QSocketNotifier>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

namespace
{

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_disconnect);
}

class X11EventReaderHelper : public QObject
{
    Q_OBJECT
public:
    explicit X11EventReaderHelper(xcb_connection_t* c);

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

}

TEST_CASE("xwayland input", "[input],[xwl]")
{
    test::setup setup("xwayland-input", base::operation_mode::xwayland);
    setup.start();
    cursor()->set_pos(QPoint(640, 512));

    SECTION("pointer enter leave")
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

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry = QRect(0, 0, 100, 200);
        const uint32_t values[] = {XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW};
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
                          XCB_CW_EVENT_MASK,
                          values);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::normal);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.last().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QVERIFY(win::decoration(client));
        QVERIFY(!client->hasStrut());
        QVERIFY(!client->isHiddenInternal());
        QVERIFY(!client->render_data.ready_for_painting);

        QVERIFY(!client->surface);
        QSignalSpy surfaceChangedSpy(client->qobject.get(), &win::window_qobject::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
        QVERIFY(client->surface);

        // Wait until the window is ready for painting, otherwise it doesn't get input events.
        TRY_REQUIRE(client->render_data.ready_for_painting);

        // move pointer into the window, should trigger an enter
        QVERIFY(!client->geo.frame.contains(cursor()->pos()));
        QVERIFY(enteredSpy.isEmpty());

        REQUIRE(!setup.base->server->seat()->pointers().get_focus().surface);
        REQUIRE(setup.base->server->seat()->pointers().get_focus().devices.empty());

        cursor()->set_pos(client->geo.frame.center());
        QCOMPARE(setup.base->server->seat()->pointers().get_focus().surface, client->surface);
        QVERIFY(!setup.base->server->seat()->pointers().get_focus().devices.empty());
        QVERIFY(enteredSpy.wait());

        // move out of window
        cursor()->set_pos(client->geo.frame.bottomRight() + QPoint(10, 10));
        QVERIFY(leftSpy.wait());

        // destroy window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }
}

}

#include "xwayland_input.moc"
