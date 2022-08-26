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
#include "base/x11/atoms.h"
#include "input/cursor.h"
#include "win/deco.h"
#include "win/rules/book.h"
#include "win/rules/ruling.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/space_reconfigure.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class WindowRuleTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testApplyInitialMaximizeVert_data();
    void testApplyInitialMaximizeVert();
    void testWindowClassChange();
};

void WindowRuleTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void WindowRuleTest::init()
{
    Test::app()->base.input->cursor->set_pos(QPoint(640, 512));
}

void WindowRuleTest::cleanup()
{
    // discards old rules
    Test::app()->base.space->rule_book->load();
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

void WindowRuleTest::testApplyInitialMaximizeVert_data()
{
    QTest::addColumn<QByteArray>("role");

    QTest::newRow("lowercase") << QByteArrayLiteral("mainwindow");
    QTest::newRow("CamelCase") << QByteArrayLiteral("MainWindow");
}

void WindowRuleTest::testApplyInitialMaximizeVert()
{
    // this test creates the situation of BUG 367554: creates a window and initial apply maximize
    // vertical the window is matched by class and role load the rule
    QFile ruleFile(QFINDTESTDATA("./data/rules/maximize-vert-apply-initial"));
    QVERIFY(ruleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QMetaObject::invokeMethod(Test::app()->base.space->rule_book.get(),
                              "temporaryRulesMessage",
                              Q_ARG(QString, QString::fromUtf8(ruleFile.readAll())));

    // create the test window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
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
    xcb_icccm_set_wm_class(c.get(), w, 9, "kpat\0kpat");

    QFETCH(QByteArray, role);
    xcb_change_property(c.get(),
                        XCB_PROP_MODE_REPLACE,
                        w,
                        Test::app()->base.space->atoms->wm_window_role,
                        XCB_ATOM_STRING,
                        8,
                        role.length(),
                        role.constData());

    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client
        = dynamic_cast<win::x11::window*>(windowCreatedSpy.last().first().value<Toplevel*>());
    QVERIFY(client);
    QVERIFY(win::decoration(client));
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());
    QTRY_VERIFY(client->ready_for_painting);
    if (!client->surface) {
        QSignalSpy surfaceChangedSpy(client, &Toplevel::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
    }
    QVERIFY(client->surface);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::vertical);

    // destroy window again
    QSignalSpy windowClosedSpy(client, &Toplevel::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

void WindowRuleTest::testWindowClassChange()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);

    auto group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", 2);
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", 1);
    group.sync();

    Test::app()->base.space->rule_book->config = config;
    win::space_reconfigure(*Test::app()->base.space);

    // create the test window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
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
    xcb_icccm_set_wm_class(c.get(), w, 23, "org.kde.bar\0org.kde.bar");

    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client
        = dynamic_cast<win::x11::window*>(windowCreatedSpy.last().first().value<Toplevel*>());
    QVERIFY(client);
    QVERIFY(win::decoration(client));
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());
    QVERIFY(!client->ready_for_painting);
    QTRY_VERIFY(client->ready_for_painting);
    if (!client->surface) {
        QSignalSpy surfaceChangedSpy(client, &Toplevel::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
    }
    QVERIFY(client->surface);
    QCOMPARE(client->control->keep_above(), false);

    // now change class
    QSignalSpy windowClassChangedSpy{client, &Toplevel::windowClassChanged};
    QVERIFY(windowClassChangedSpy.isValid());
    xcb_icccm_set_wm_class(c.get(), w, 23, "org.kde.foo\0org.kde.foo");
    xcb_flush(c.get());
    QVERIFY(windowClassChangedSpy.wait());
    QCOMPARE(client->control->keep_above(), true);

    // destroy window
    QSignalSpy windowClosedSpy(client, &Toplevel::closed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::WindowRuleTest)
#include "window_rules_test.moc"
