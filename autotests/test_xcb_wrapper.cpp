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
// KWin
#include "../xcbutils.h"
// Qt
#include <QApplication>
#include <QX11Info>
#include <QtTest>
#include <netwm.h>
// xcb
#include <xcb/xcb.h>

using namespace KWin;

class TestXcbWrapper : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void defaultCtor();
    void normalCtor();
    void copyCtorEmpty();
    void copyCtorBeforeRetrieve();
    void copyCtorAfterRetrieve();
    void assignementEmpty();
    void assignmentBeforeRetrieve();
    void assignmentAfterRetrieve();
    void discard();
    void testQueryTree();
    void testCurrentInput();
    void testTransientFor();
    void testPropertyByteArray();
    void testPropertyBool();
    void testAtom();
    void testMotifEmpty();
    void testMotif_data();
    void testMotif();

private:
    void testEmpty(base::x11::xcb::window_geometry& geometry);
    void testGeometry(base::x11::xcb::window_geometry& geometry, const QRect& rect);
    base::x11::xcb::window m_testWindow;
};

void TestXcbWrapper::initTestCase()
{
    qApp->setProperty("x11RootWindow", QVariant::fromValue<quint32>(QX11Info::appRootWindow()));
    qApp->setProperty("x11Connection", QVariant::fromValue<void*>(QX11Info::connection()));
}

void TestXcbWrapper::init()
{
    const uint32_t values[] = {true};
    m_testWindow.create(
        QRect(0, 0, 10, 10), XCB_WINDOW_CLASS_INPUT_ONLY, XCB_CW_OVERRIDE_REDIRECT, values);
    QVERIFY(m_testWindow.is_valid());
}

void TestXcbWrapper::cleanup()
{
    m_testWindow.reset();
}

void TestXcbWrapper::testEmpty(base::x11::xcb::window_geometry& geometry)
{
    QCOMPARE(geometry.window(), noneWindow());
    QVERIFY(!geometry.data());
    QCOMPARE(geometry.is_null(), true);
    QCOMPARE(geometry.rect(), QRect());
    QVERIFY(!geometry);
}

void TestXcbWrapper::testGeometry(base::x11::xcb::window_geometry& geometry, const QRect& rect)
{
    QCOMPARE(geometry.window(), (xcb_window_t)m_testWindow);
    // now lets retrieve some data
    QCOMPARE(geometry.rect(), rect);
    QVERIFY(geometry.is_retrieved());
    QCOMPARE(geometry.is_null(), false);
    QVERIFY(geometry);
    QVERIFY(geometry.data());
    QCOMPARE(geometry.data()->x, int16_t(rect.x()));
    QCOMPARE(geometry.data()->y, int16_t(rect.y()));
    QCOMPARE(geometry.data()->width, uint16_t(rect.width()));
    QCOMPARE(geometry.data()->height, uint16_t(rect.height()));
}

void TestXcbWrapper::defaultCtor()
{
    base::x11::xcb::window_geometry geometry;
    testEmpty(geometry);
    QVERIFY(!geometry.is_retrieved());
}

void TestXcbWrapper::normalCtor()
{
    base::x11::xcb::window_geometry geometry(m_testWindow);
    QVERIFY(!geometry.is_retrieved());
    testGeometry(geometry, QRect(0, 0, 10, 10));
}

void TestXcbWrapper::copyCtorEmpty()
{
    base::x11::xcb::window_geometry geometry;
    base::x11::xcb::window_geometry other(geometry);
    testEmpty(geometry);
    QVERIFY(geometry.is_retrieved());
    testEmpty(other);
    QVERIFY(!other.is_retrieved());
}

void TestXcbWrapper::copyCtorBeforeRetrieve()
{
    base::x11::xcb::window_geometry geometry(m_testWindow);
    QVERIFY(!geometry.is_retrieved());
    base::x11::xcb::window_geometry other(geometry);
    testEmpty(geometry);
    QVERIFY(geometry.is_retrieved());

    QVERIFY(!other.is_retrieved());
    testGeometry(other, QRect(0, 0, 10, 10));
}

void TestXcbWrapper::copyCtorAfterRetrieve()
{
    base::x11::xcb::window_geometry geometry(m_testWindow);
    QVERIFY(geometry);
    QVERIFY(geometry.is_retrieved());
    QCOMPARE(geometry.rect(), QRect(0, 0, 10, 10));
    base::x11::xcb::window_geometry other(geometry);
    testEmpty(geometry);
    QVERIFY(geometry.is_retrieved());

    QVERIFY(other.is_retrieved());
    testGeometry(other, QRect(0, 0, 10, 10));
}

void TestXcbWrapper::assignementEmpty()
{
    base::x11::xcb::window_geometry geometry;
    base::x11::xcb::window_geometry other;
    testEmpty(geometry);
    testEmpty(other);

    other = geometry;
    QVERIFY(geometry.is_retrieved());
    testEmpty(geometry);
    testEmpty(other);
    QVERIFY(!other.is_retrieved());

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_CLANG("-Wself-assign-overloaded")
    // test assignment to self
    geometry = geometry;
    other = other;
    testEmpty(geometry);
    testEmpty(other);
    QT_WARNING_POP
}

void TestXcbWrapper::assignmentBeforeRetrieve()
{
    base::x11::xcb::window_geometry geometry(m_testWindow);
    base::x11::xcb::window_geometry other = geometry;
    QVERIFY(geometry.is_retrieved());
    testEmpty(geometry);

    QVERIFY(!other.is_retrieved());
    testGeometry(other, QRect(0, 0, 10, 10));

    other = base::x11::xcb::window_geometry(m_testWindow);
    QVERIFY(!other.is_retrieved());
    QCOMPARE(other.window(), (xcb_window_t)m_testWindow);
    other = base::x11::xcb::window_geometry();
    testEmpty(geometry);

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_CLANG("-Wself-assign-overloaded")
    // test assignment to self
    geometry = geometry;
    other = other;
    testEmpty(geometry);
    QT_WARNING_POP
}

void TestXcbWrapper::assignmentAfterRetrieve()
{
    base::x11::xcb::window_geometry geometry(m_testWindow);
    QVERIFY(geometry);
    QVERIFY(geometry.is_retrieved());
    base::x11::xcb::window_geometry other = geometry;
    testEmpty(geometry);

    QVERIFY(other.is_retrieved());
    testGeometry(other, QRect(0, 0, 10, 10));

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_CLANG("-Wself-assign-overloaded")
    // test assignment to self
    geometry = geometry;
    other = other;
    testEmpty(geometry);
    testGeometry(other, QRect(0, 0, 10, 10));
    QT_WARNING_POP

    // set to empty again
    other = base::x11::xcb::window_geometry();
    testEmpty(other);
}

void TestXcbWrapper::discard()
{
    // discard of reply cannot be tested properly as we cannot check whether the reply has been
    // discarded therefore it's more or less just a test to ensure that it doesn't crash and the
    // code paths are taken.
    base::x11::xcb::window_geometry* geometry = new base::x11::xcb::window_geometry();
    delete geometry;

    geometry = new base::x11::xcb::window_geometry(m_testWindow);
    delete geometry;

    geometry = new base::x11::xcb::window_geometry(m_testWindow);
    QVERIFY(geometry->data());
    delete geometry;
}

void TestXcbWrapper::testQueryTree()
{
    base::x11::xcb::tree tree(m_testWindow);
    // should have root as parent
    QCOMPARE(tree.parent(), static_cast<xcb_window_t>(QX11Info::appRootWindow()));
    // shouldn't have any children
    QCOMPARE(tree->children_len, uint16_t(0));
    QVERIFY(!tree.children());

    // query for root
    base::x11::xcb::tree root(QX11Info::appRootWindow());
    // shouldn't have a parent
    QCOMPARE(root.parent(), xcb_window_t(XCB_WINDOW_NONE));
    QVERIFY(root->children_len > 0);
    xcb_window_t* children = root.children();
    bool found = false;
    for (int i = 0; i < xcb_query_tree_children_length(root.data()); ++i) {
        if (children[i] == tree.window()) {
            found = true;
            break;
        }
    }
    QVERIFY(found);

    // query for not existing window
    base::x11::xcb::tree doesntExist(XCB_WINDOW_NONE);
    QCOMPARE(doesntExist.parent(), xcb_window_t(XCB_WINDOW_NONE));
    QVERIFY(doesntExist.is_null());
    QVERIFY(doesntExist.is_retrieved());
}

void TestXcbWrapper::testCurrentInput()
{
    xcb_connection_t* c = QX11Info::connection();
    m_testWindow.map();
    QX11Info::setAppTime(QX11Info::getTimestamp());

    // let's set the input focus
    m_testWindow.focus(XCB_INPUT_FOCUS_PARENT, QX11Info::appTime());
    xcb_flush(c);

    base::x11::xcb::current_input input;
    QCOMPARE(input.window(), (xcb_window_t)m_testWindow);

    // creating a copy should make the input object have no window any more
    base::x11::xcb::current_input input2(input);
    QCOMPARE(input2.window(), (xcb_window_t)m_testWindow);
    QCOMPARE(input.window(), xcb_window_t(XCB_WINDOW_NONE));
}

void TestXcbWrapper::testTransientFor()
{
    base::x11::xcb::transient_for transient(m_testWindow);
    QCOMPARE(transient.window(), (xcb_window_t)m_testWindow);

    // our m_testWindow doesn't have a transient for hint
    xcb_window_t compareWindow = XCB_WINDOW_NONE;
    QVERIFY(!transient.get_transient_for(&compareWindow));
    QCOMPARE(compareWindow, xcb_window_t(XCB_WINDOW_NONE));
    bool ok = true;
    QCOMPARE(transient.value<xcb_window_t>(32, XCB_ATOM_WINDOW, XCB_WINDOW_NONE, &ok),
             xcb_window_t(XCB_WINDOW_NONE));
    QVERIFY(!ok);
    ok = true;
    QCOMPARE(transient.value<xcb_window_t>(XCB_WINDOW_NONE, &ok), xcb_window_t(XCB_WINDOW_NONE));
    QVERIFY(!ok);

    // Create a Window with a transient for hint
    base::x11::xcb::window transientWindow(createWindow());
    xcb_window_t testWindowId = m_testWindow;
    transientWindow.change_property(
        XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1, &testWindowId);

    // let's get another transient object
    base::x11::xcb::transient_for realTransient(transientWindow);
    QVERIFY(realTransient.get_transient_for(&compareWindow));
    QCOMPARE(compareWindow, (xcb_window_t)m_testWindow);
    ok = false;
    QCOMPARE(realTransient.value<xcb_window_t>(32, XCB_ATOM_WINDOW, XCB_WINDOW_NONE, &ok),
             (xcb_window_t)m_testWindow);
    QVERIFY(ok);
    ok = false;
    QCOMPARE(realTransient.value<xcb_window_t>(XCB_WINDOW_NONE, &ok), (xcb_window_t)m_testWindow);
    QVERIFY(ok);
    ok = false;
    QCOMPARE(realTransient.value<xcb_window_t>(), (xcb_window_t)m_testWindow);
    QCOMPARE(realTransient.value<xcb_window_t*>(nullptr, &ok)[0], (xcb_window_t)m_testWindow);
    QVERIFY(ok);
    QCOMPARE(realTransient.value<xcb_window_t*>()[0], (xcb_window_t)m_testWindow);

    // test for a not existing window
    base::x11::xcb::transient_for doesntExist(XCB_WINDOW_NONE);
    QVERIFY(!doesntExist.get_transient_for(&compareWindow));
}

void TestXcbWrapper::testPropertyByteArray()
{
    base::x11::xcb::window testWindow(createWindow());
    base::x11::xcb::property prop(false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
    QCOMPARE(prop.to_byte_array(), QByteArray());
    bool ok = true;
    QCOMPARE(prop.to_byte_array(&ok), QByteArray());
    QVERIFY(!ok);
    ok = true;
    QVERIFY(!prop.value<const char*>());
    QCOMPARE(prop.value<const char*>("bar", &ok), "bar");
    QVERIFY(!ok);
    QCOMPARE(QByteArray(base::x11::xcb::string_property(testWindow, XCB_ATOM_WM_NAME)),
             QByteArray());

    testWindow.change_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 3, "foo");
    prop
        = base::x11::xcb::property(false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
    QCOMPARE(prop.to_byte_array(), QByteArrayLiteral("foo"));
    QCOMPARE(prop.to_byte_array(&ok), QByteArrayLiteral("foo"));
    QVERIFY(ok);
    QCOMPARE(prop.value<const char*>(nullptr, &ok), "foo");
    QVERIFY(ok);
    QCOMPARE(QByteArray(base::x11::xcb::string_property(testWindow, XCB_ATOM_WM_NAME)),
             QByteArrayLiteral("foo"));

    // verify incorrect format and type
    QCOMPARE(prop.to_byte_array(32), QByteArray());
    QCOMPARE(prop.to_byte_array(8, XCB_ATOM_CARDINAL), QByteArray());

    // verify empty property
    testWindow.change_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 0, nullptr);
    prop
        = base::x11::xcb::property(false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
    QCOMPARE(prop.to_byte_array(), QByteArray());
    QCOMPARE(prop.to_byte_array(&ok), QByteArray());
    // valid bytearray
    QVERIFY(ok);
    // The bytearray should be empty
    QVERIFY(prop.to_byte_array().isEmpty());
    // The bytearray should be not null
    QVERIFY(!prop.to_byte_array().isNull());
    QVERIFY(!prop.value<const char*>());
    QCOMPARE(QByteArray(base::x11::xcb::string_property(testWindow, XCB_ATOM_WM_NAME)),
             QByteArray());

    // verify non existing property
    base::x11::xcb::atom invalid(QByteArrayLiteral("INVALID_ATOM"), connection());
    prop = base::x11::xcb::property(false, testWindow, invalid, XCB_ATOM_STRING, 0, 100000);
    QCOMPARE(prop.to_byte_array(), QByteArray());
    QCOMPARE(prop.to_byte_array(&ok), QByteArray());
    // invalid bytearray
    QVERIFY(!ok);
    // The bytearray should be empty
    QVERIFY(prop.to_byte_array().isEmpty());
    // The bytearray should be not null
    QVERIFY(prop.to_byte_array().isNull());
    QVERIFY(!prop.value<const char*>());
    QCOMPARE(QByteArray(base::x11::xcb::string_property(testWindow, XCB_ATOM_WM_NAME)),
             QByteArray());
}

void TestXcbWrapper::testPropertyBool()
{
    base::x11::xcb::window testWindow(createWindow());
    base::x11::xcb::atom blockCompositing(QByteArrayLiteral("_KDE_NET_WM_BLOCK_COMPOSITING"),
                                          connection());
    QVERIFY(blockCompositing != XCB_ATOM_NONE);
    NETWinInfo info(QX11Info::connection(),
                    testWindow,
                    QX11Info::appRootWindow(),
                    NET::Properties(),
                    NET::WM2BlockCompositing);

    base::x11::xcb::property prop(
        false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
    bool ok = true;
    QVERIFY(!prop.to_bool());
    QVERIFY(!prop.to_bool(&ok));
    QVERIFY(!ok);

    info.setBlockingCompositing(true);
    xcb_flush(QX11Info::connection());
    prop = base::x11::xcb::property(
        false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
    QVERIFY(prop.to_bool());
    QVERIFY(prop.to_bool(&ok));
    QVERIFY(ok);

    // incorrect type and format
    QVERIFY(!prop.to_bool(8));
    QVERIFY(!prop.to_bool(32, blockCompositing));
    QVERIFY(!prop.to_bool(32, blockCompositing, &ok));
    QVERIFY(!ok);

    // incorrect value:
    uint32_t d[] = {1, 0};
    testWindow.change_property(blockCompositing, XCB_ATOM_CARDINAL, 32, 2, d);
    prop = base::x11::xcb::property(
        false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
    QVERIFY(!prop.to_bool());
    ok = true;
    QVERIFY(!prop.to_bool(&ok));
    QVERIFY(!ok);
}

void TestXcbWrapper::testAtom()
{
    base::x11::xcb::atom atom(QByteArrayLiteral("WM_CLIENT_MACHINE"), connection());
    QCOMPARE(atom.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));
    QVERIFY(atom == XCB_ATOM_WM_CLIENT_MACHINE);
    QVERIFY(atom.is_valid());

    // test the const paths
    base::x11::xcb::atom const& atom2(atom);
    QVERIFY(atom2.is_valid());
    QVERIFY(atom2 == XCB_ATOM_WM_CLIENT_MACHINE);
    QCOMPARE(atom2.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));

    // destroy before retrieved
    base::x11::xcb::atom atom3(QByteArrayLiteral("WM_CLIENT_MACHINE"), connection());
    QCOMPARE(atom3.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));
}

void TestXcbWrapper::testMotifEmpty()
{
    base::x11::xcb::atom atom(QByteArrayLiteral("_MOTIF_WM_HINTS"), connection());
    base::x11::xcb::motif_hints hints(atom);

    // pre init
    QCOMPARE(hints.has_decoration(), false);
    QCOMPARE(hints.no_border(), false);
    QCOMPARE(hints.resize(), true);
    QCOMPARE(hints.move(), true);
    QCOMPARE(hints.minimize(), true);
    QCOMPARE(hints.maximize(), true);
    QCOMPARE(hints.close(), true);

    // post init, pre read
    hints.init(m_testWindow);
    QCOMPARE(hints.has_decoration(), false);
    QCOMPARE(hints.no_border(), false);
    QCOMPARE(hints.resize(), true);
    QCOMPARE(hints.move(), true);
    QCOMPARE(hints.minimize(), true);
    QCOMPARE(hints.maximize(), true);
    QCOMPARE(hints.close(), true);

    // post read
    hints.read();
    QCOMPARE(hints.has_decoration(), false);
    QCOMPARE(hints.no_border(), false);
    QCOMPARE(hints.resize(), true);
    QCOMPARE(hints.move(), true);
    QCOMPARE(hints.minimize(), true);
    QCOMPARE(hints.maximize(), true);
    QCOMPARE(hints.close(), true);
}

void TestXcbWrapper::testMotif_data()
{
    QTest::addColumn<quint32>("flags");
    QTest::addColumn<quint32>("functions");
    QTest::addColumn<quint32>("decorations");

    QTest::addColumn<bool>("expectedHasDecoration");
    QTest::addColumn<bool>("expectedNoBorder");
    QTest::addColumn<bool>("expectedResize");
    QTest::addColumn<bool>("expectedMove");
    QTest::addColumn<bool>("expectedMinimize");
    QTest::addColumn<bool>("expectedMaximize");
    QTest::addColumn<bool>("expectedClose");

    QTest::newRow("none") << 0u << 0u << 0u << false << false << true << true << true << true
                          << true;
    QTest::newRow("noborder") << 2u << 5u << 0u << true << true << true << true << true << true
                              << true;
    QTest::newRow("border") << 2u << 5u << 1u << true << false << true << true << true << true
                            << true;
    QTest::newRow("resize") << 1u << 2u << 1u << false << false << true << false << false << false
                            << false;
    QTest::newRow("move") << 1u << 4u << 1u << false << false << false << true << false << false
                          << false;
    QTest::newRow("minimize") << 1u << 8u << 1u << false << false << false << false << true << false
                              << false;
    QTest::newRow("maximize") << 1u << 16u << 1u << false << false << false << false << false
                              << true << false;
    QTest::newRow("close") << 1u << 32u << 1u << false << false << false << false << false << false
                           << true;

    QTest::newRow("resize/all") << 1u << 3u << 1u << false << false << false << true << true << true
                                << true;
    QTest::newRow("move/all") << 1u << 5u << 1u << false << false << true << false << true << true
                              << true;
    QTest::newRow("minimize/all") << 1u << 9u << 1u << false << false << true << true << false
                                  << true << true;
    QTest::newRow("maximize/all") << 1u << 17u << 1u << false << false << true << true << true
                                  << false << true;
    QTest::newRow("close/all") << 1u << 33u << 1u << false << false << true << true << true << true
                               << false;

    QTest::newRow("all") << 1u << 62u << 1u << false << false << true << true << true << true
                         << true;
    QTest::newRow("all/all") << 1u << 63u << 1u << false << false << false << false << false
                             << false << false;
    QTest::newRow("all/all/deco") << 3u << 63u << 1u << true << false << false << false << false
                                  << false << false;
}

void TestXcbWrapper::testMotif()
{
    base::x11::xcb::atom atom(QByteArrayLiteral("_MOTIF_WM_HINTS"), connection());
    QFETCH(quint32, flags);
    QFETCH(quint32, functions);
    QFETCH(quint32, decorations);

    quint32 data[] = {flags, functions, decorations, 0, 0};
    xcb_change_property(
        QX11Info::connection(), XCB_PROP_MODE_REPLACE, m_testWindow, atom, atom, 32, 5, data);
    xcb_flush(QX11Info::connection());

    base::x11::xcb::motif_hints hints(atom);
    hints.init(m_testWindow);
    hints.read();
    QTEST(hints.has_decoration(), "expectedHasDecoration");
    QTEST(hints.no_border(), "expectedNoBorder");
    QTEST(hints.resize(), "expectedResize");
    QTEST(hints.move(), "expectedMove");
    QTEST(hints.minimize(), "expectedMinimize");
    QTEST(hints.maximize(), "expectedMaximize");
    QTEST(hints.close(), "expectedClose");
}

Q_CONSTRUCTOR_FUNCTION(forceXcb)
QTEST_MAIN(TestXcbWrapper)
#include "test_xcb_wrapper.moc"
