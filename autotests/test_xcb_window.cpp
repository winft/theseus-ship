/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "testutils.h"

#include "base/x11/xcb/proto.h"
#include "base/x11/xcb/window.h"

#include <QApplication>
#include <QX11Info>
#include <QtTest>

using namespace KWin;

class TestXcbWindow : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void defaultCtor();
    void ctor();
    void classCtor();
    void create();
    void mapUnmap();
    void geometry();
    void destroy();
    void destroyNotManaged();
};

void TestXcbWindow::initTestCase()
{
    qApp->setProperty("x11RootWindow", QVariant::fromValue<quint32>(QX11Info::appRootWindow()));
    qApp->setProperty("x11Connection", QVariant::fromValue<void*>(QX11Info::connection()));
}

void TestXcbWindow::defaultCtor()
{
    base::x11::xcb::window window;
    QCOMPARE(window.is_valid(), false);
    xcb_window_t wId = window;
    QCOMPARE(wId, noneWindow());

    xcb_window_t nativeWindow = createWindow();
    base::x11::xcb::window window2(nativeWindow);
    QCOMPARE(window2.is_valid(), true);
    wId = window2;
    QCOMPARE(wId, nativeWindow);
}

void TestXcbWindow::ctor()
{
    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(geo, XCB_CW_OVERRIDE_REDIRECT, values);
    QCOMPARE(window.is_valid(), true);
    QVERIFY(window != XCB_WINDOW_NONE);
    base::x11::xcb::geometry windowGeometry(window);
    QCOMPARE(windowGeometry.is_null(), false);
    QCOMPARE(windowGeometry.rect(), geo);
}

void TestXcbWindow::classCtor()
{
    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(
        geo, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_CW_OVERRIDE_REDIRECT, values);
    QCOMPARE(window.is_valid(), true);
    QVERIFY(window != XCB_WINDOW_NONE);
    base::x11::xcb::geometry windowGeometry(window);
    QCOMPARE(windowGeometry.is_null(), false);
    QCOMPARE(windowGeometry.rect(), geo);

    base::x11::xcb::window_attributes attribs(window);
    QCOMPARE(attribs.is_null(), false);
    QVERIFY(attribs->_class == XCB_WINDOW_CLASS_INPUT_ONLY);
}

void TestXcbWindow::create()
{
    base::x11::xcb::window window;
    QCOMPARE(window.is_valid(), false);
    xcb_window_t wId = window;
    QCOMPARE(wId, noneWindow());

    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    window.create(geo, XCB_CW_OVERRIDE_REDIRECT, values);
    QCOMPARE(window.is_valid(), true);
    QVERIFY(window != XCB_WINDOW_NONE);
    // and reset again
    window.reset();
    QCOMPARE(window.is_valid(), false);
    QVERIFY(window == XCB_WINDOW_NONE);
}

void TestXcbWindow::mapUnmap()
{
    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(
        geo, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_CW_OVERRIDE_REDIRECT, values);
    base::x11::xcb::window_attributes attribs(window);
    QCOMPARE(attribs.is_null(), false);
    QVERIFY(attribs->map_state == XCB_MAP_STATE_UNMAPPED);

    window.map();
    base::x11::xcb::window_attributes attribs2(window);
    QCOMPARE(attribs2.is_null(), false);
    QVERIFY(attribs2->map_state != XCB_MAP_STATE_UNMAPPED);

    window.unmap();
    base::x11::xcb::window_attributes attribs3(window);
    QCOMPARE(attribs3.is_null(), false);
    QVERIFY(attribs3->map_state == XCB_MAP_STATE_UNMAPPED);

    // map, unmap shouldn't fail for an invalid window, it's just ignored
    window.reset();
    window.map();
    window.unmap();
}

void TestXcbWindow::geometry()
{
    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(
        geo, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_CW_OVERRIDE_REDIRECT, values);
    base::x11::xcb::geometry windowGeometry(window);
    QCOMPARE(windowGeometry.is_null(), false);
    QCOMPARE(windowGeometry.rect(), geo);

    const QRect geo2(10, 20, 100, 200);
    window.set_geometry(geo2);
    base::x11::xcb::geometry windowGeometry2(window);
    QCOMPARE(windowGeometry2.is_null(), false);
    QCOMPARE(windowGeometry2.rect(), geo2);

    // setting a geometry on an invalid window should be ignored
    window.reset();
    window.set_geometry(geo2);
    base::x11::xcb::geometry windowGeometry3(window);
    QCOMPARE(windowGeometry3.is_null(), true);
}

void TestXcbWindow::destroy()
{
    const QRect geo(0, 0, 10, 10);
    const uint32_t values[] = {true};
    base::x11::xcb::window window(geo, XCB_CW_OVERRIDE_REDIRECT, values);
    QCOMPARE(window.is_valid(), true);
    xcb_window_t wId = window;

    window.create(geo, XCB_CW_OVERRIDE_REDIRECT, values);
    // wId should now be invalid
    xcb_generic_error_t* error = nullptr;
    unique_cptr<xcb_get_window_attributes_reply_t> attribs(xcb_get_window_attributes_reply(
        connection(), xcb_get_window_attributes(connection(), wId), &error));
    QVERIFY(!attribs);
    QCOMPARE(error->error_code, uint8_t(3));
    QCOMPARE(error->resource_id, wId);
    free(error);

    // test the same for the dtor
    {
        base::x11::xcb::window scopedWindow(geo, XCB_CW_OVERRIDE_REDIRECT, values);
        QVERIFY(scopedWindow.is_valid());
        wId = scopedWindow;
    }
    error = nullptr;
    unique_cptr<xcb_get_window_attributes_reply_t> attribs2(xcb_get_window_attributes_reply(
        connection(), xcb_get_window_attributes(connection(), wId), &error));
    QVERIFY(!attribs2);
    QCOMPARE(error->error_code, uint8_t(3));
    QCOMPARE(error->resource_id, wId);
    free(error);
}

void TestXcbWindow::destroyNotManaged()
{
    base::x11::xcb::window window;
    // just destroy the non-existing window
    window.reset();

    // now let's add a window
    window.reset(createWindow(), false);
    xcb_window_t w = window;
    window.reset();
    base::x11::xcb::window_attributes attribs(w);
    QVERIFY(attribs);
}

Q_CONSTRUCTOR_FUNCTION(forceXcb)
QTEST_MAIN(TestXcbWindow)
#include "test_xcb_window.moc"
