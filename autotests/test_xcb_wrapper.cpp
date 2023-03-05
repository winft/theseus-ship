/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/setup.h"

#include "base/x11/xcb/atom.h"
#include "base/x11/xcb/motif_hints.h"
#include "base/x11/xcb/proto.h"
#include "base/x11/xcb/window.h"
#include "win/x11/net/win_info.h"

#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb.h>

namespace KWin::detail::test
{

TEST_CASE("xcb wrapper", "[win],[xwl]")
{
    test::setup setup("xcb-wrapper", base::operation_mode::xwayland);
    setup.start();

    auto connection = setup.base->x11_data.connection;
    auto root_window = setup.base->x11_data.root_window;

    const uint32_t values[] = {true};
    base::x11::xcb::window m_testWindow;
    m_testWindow.create(connection,
                        root_window,
                        QRect(0, 0, 10, 10),
                        XCB_WINDOW_CLASS_INPUT_ONLY,
                        XCB_CW_OVERRIDE_REDIRECT,
                        values);
    QVERIFY(m_testWindow.is_valid());

    auto createWindow = [&]() {
        xcb_window_t w = xcb_generate_id(connection);
        uint32_t const values[] = {true};
        xcb_create_window(connection,
                          0,
                          w,
                          root_window,
                          0,
                          0,
                          10,
                          10,
                          0,
                          XCB_WINDOW_CLASS_INPUT_ONLY,
                          XCB_COPY_FROM_PARENT,
                          XCB_CW_OVERRIDE_REDIRECT,
                          values);
        return w;
    };

    auto testEmpty = [](base::x11::xcb::geometry& geometry) {
        REQUIRE(geometry.window() == XCB_WINDOW_NONE);
        QVERIFY(!geometry.data());
        QCOMPARE(geometry.is_null(), true);
        QCOMPARE(geometry.rect(), QRect());
        QVERIFY(!geometry);
    };

    auto testGeometry = [&](base::x11::xcb::geometry& geometry, QRect const& rect) {
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
    };

    SECTION("default ctor")
    {
        base::x11::xcb::geometry geometry(connection);
        testEmpty(geometry);
        QVERIFY(!geometry.is_retrieved());
    }

    SECTION("normal ctor")
    {
        base::x11::xcb::geometry geometry(connection, m_testWindow);
        QVERIFY(!geometry.is_retrieved());
        testGeometry(geometry, QRect(0, 0, 10, 10));
    }

    SECTION("copy ctor empty")
    {
        base::x11::xcb::geometry geometry(connection);
        base::x11::xcb::geometry other(geometry);
        testEmpty(geometry);
        QVERIFY(geometry.is_retrieved());
        testEmpty(other);
        QVERIFY(!other.is_retrieved());
    }

    SECTION("copy ctor before retrieve")
    {
        base::x11::xcb::geometry geometry(connection, m_testWindow);
        QVERIFY(!geometry.is_retrieved());
        base::x11::xcb::geometry other(geometry);
        testEmpty(geometry);
        QVERIFY(geometry.is_retrieved());

        QVERIFY(!other.is_retrieved());
        testGeometry(other, QRect(0, 0, 10, 10));
    }

    SECTION("copy ctor after retrieve")
    {
        base::x11::xcb::geometry geometry(connection, m_testWindow);
        QVERIFY(geometry);
        QVERIFY(geometry.is_retrieved());
        QCOMPARE(geometry.rect(), QRect(0, 0, 10, 10));
        base::x11::xcb::geometry other(geometry);
        testEmpty(geometry);
        QVERIFY(geometry.is_retrieved());

        QVERIFY(other.is_retrieved());
        testGeometry(other, QRect(0, 0, 10, 10));
    }

    SECTION("assignement empty")
    {
        base::x11::xcb::geometry geometry(connection);
        base::x11::xcb::geometry other(connection);
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

    SECTION("assignement before retrieve")
    {
        base::x11::xcb::geometry geometry(connection, m_testWindow);
        base::x11::xcb::geometry other = geometry;
        QVERIFY(geometry.is_retrieved());
        testEmpty(geometry);

        QVERIFY(!other.is_retrieved());
        testGeometry(other, QRect(0, 0, 10, 10));

        other = base::x11::xcb::geometry(connection, m_testWindow);
        QVERIFY(!other.is_retrieved());
        QCOMPARE(other.window(), (xcb_window_t)m_testWindow);
        other = base::x11::xcb::geometry(connection);
        testEmpty(geometry);

        QT_WARNING_PUSH
        QT_WARNING_DISABLE_CLANG("-Wself-assign-overloaded")
        // test assignment to self
        geometry = geometry;
        other = other;
        testEmpty(geometry);
        QT_WARNING_POP
    }

    SECTION("assignement after retrieve")
    {
        base::x11::xcb::geometry geometry(connection, m_testWindow);
        QVERIFY(geometry);
        QVERIFY(geometry.is_retrieved());
        base::x11::xcb::geometry other = geometry;
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
        other = base::x11::xcb::geometry(connection);
        testEmpty(other);
    }

    SECTION("discard")
    {
        // discard of reply cannot be tested properly as we cannot check whether the reply has been
        // discarded therefore it's more or less just a test to ensure that it doesn't crash and the
        // code paths are taken.
        auto geometry = new base::x11::xcb::geometry(connection);
        delete geometry;

        geometry = new base::x11::xcb::geometry(connection, m_testWindow);
        delete geometry;

        geometry = new base::x11::xcb::geometry(connection, m_testWindow);
        QVERIFY(geometry->data());
        delete geometry;
    }

    SECTION("query tree")
    {
        base::x11::xcb::tree tree(connection, m_testWindow);

        // should have root as parent
        REQUIRE(tree.parent() == root_window);

        // shouldn't have any children
        QCOMPARE(tree->children_len, uint16_t(0));
        QVERIFY(!tree.children());

        // query for root
        base::x11::xcb::tree root(connection, root_window);
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
        base::x11::xcb::tree doesntExist(connection, XCB_WINDOW_NONE);
        QCOMPARE(doesntExist.parent(), xcb_window_t(XCB_WINDOW_NONE));
        QVERIFY(doesntExist.is_null());
        QVERIFY(doesntExist.is_retrieved());
    }

    SECTION("current input")
    {
        m_testWindow.map();

        // let's set the input focus
        m_testWindow.focus(XCB_INPUT_FOCUS_PARENT, setup.base->x11_data.time);
        xcb_flush(connection);

        base::x11::xcb::input_focus input(connection);
        QCOMPARE(input.window(), (xcb_window_t)m_testWindow);

        // creating a copy should make the input object have no window any more
        base::x11::xcb::input_focus input2(input);
        QCOMPARE(input2.window(), (xcb_window_t)m_testWindow);
        QCOMPARE(input.window(), xcb_window_t(XCB_WINDOW_NONE));
    }

    SECTION("transient for")
    {
        base::x11::xcb::transient_for transient(connection, m_testWindow);
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
        QCOMPARE(transient.value<xcb_window_t>(XCB_WINDOW_NONE, &ok),
                 xcb_window_t(XCB_WINDOW_NONE));
        QVERIFY(!ok);

        // Create a Window with a transient for hint
        base::x11::xcb::window transientWindow(connection, createWindow());
        xcb_window_t testWindowId = m_testWindow;
        transientWindow.change_property(
            XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1, &testWindowId);

        // let's get another transient object
        base::x11::xcb::transient_for realTransient(connection, transientWindow);
        QVERIFY(realTransient.get_transient_for(&compareWindow));
        QCOMPARE(compareWindow, (xcb_window_t)m_testWindow);
        ok = false;
        QCOMPARE(realTransient.value<xcb_window_t>(32, XCB_ATOM_WINDOW, XCB_WINDOW_NONE, &ok),
                 (xcb_window_t)m_testWindow);
        QVERIFY(ok);
        ok = false;
        QCOMPARE(realTransient.value<xcb_window_t>(XCB_WINDOW_NONE, &ok),
                 (xcb_window_t)m_testWindow);
        QVERIFY(ok);
        ok = false;
        QCOMPARE(realTransient.value<xcb_window_t>(), (xcb_window_t)m_testWindow);
        QCOMPARE(realTransient.value<xcb_window_t*>(nullptr, &ok)[0], (xcb_window_t)m_testWindow);
        QVERIFY(ok);
        QCOMPARE(realTransient.value<xcb_window_t*>()[0], (xcb_window_t)m_testWindow);

        // test for a not existing window
        base::x11::xcb::transient_for doesntExist(connection, XCB_WINDOW_NONE);
        QVERIFY(!doesntExist.get_transient_for(&compareWindow));
    }

    SECTION("property byte array")
    {
        base::x11::xcb::window testWindow(connection, createWindow());
        base::x11::xcb::property prop(
            connection, false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
        QCOMPARE(prop.to_byte_array(), QByteArray());
        bool ok = true;
        QCOMPARE(prop.to_byte_array(&ok), QByteArray());
        QVERIFY(!ok);
        ok = true;
        QVERIFY(!prop.value<const char*>());
        QCOMPARE(prop.value<const char*>("bar", &ok), "bar");
        QVERIFY(!ok);
        QCOMPARE(
            QByteArray(base::x11::xcb::string_property(connection, testWindow, XCB_ATOM_WM_NAME)),
            QByteArray());

        testWindow.change_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 3, "foo");
        prop = base::x11::xcb::property(
            connection, false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
        QCOMPARE(prop.to_byte_array(), QByteArrayLiteral("foo"));
        QCOMPARE(prop.to_byte_array(&ok), QByteArrayLiteral("foo"));
        QVERIFY(ok);
        REQUIRE(prop.value<const char*>(nullptr, &ok) == std::string("foo"));
        QVERIFY(ok);
        QCOMPARE(
            QByteArray(base::x11::xcb::string_property(connection, testWindow, XCB_ATOM_WM_NAME)),
            QByteArrayLiteral("foo"));

        // verify incorrect format and type
        QCOMPARE(prop.to_byte_array(32), QByteArray());
        QCOMPARE(prop.to_byte_array(8, XCB_ATOM_CARDINAL), QByteArray());

        // verify empty property
        testWindow.change_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 0, nullptr);
        prop = base::x11::xcb::property(
            connection, false, testWindow, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 100000);
        QCOMPARE(prop.to_byte_array(), QByteArray());
        QCOMPARE(prop.to_byte_array(&ok), QByteArray());
        // valid bytearray
        QVERIFY(ok);
        // The bytearray should be empty
        QVERIFY(prop.to_byte_array().isEmpty());
        // The bytearray should be not null
        QVERIFY(!prop.to_byte_array().isNull());
        QVERIFY(!prop.value<const char*>());
        QCOMPARE(
            QByteArray(base::x11::xcb::string_property(connection, testWindow, XCB_ATOM_WM_NAME)),
            QByteArray());

        // verify non existing property
        base::x11::xcb::atom invalid(QByteArrayLiteral("INVALID_ATOM"), connection);
        prop = base::x11::xcb::property(
            connection, false, testWindow, invalid, XCB_ATOM_STRING, 0, 100000);
        QCOMPARE(prop.to_byte_array(), QByteArray());
        QCOMPARE(prop.to_byte_array(&ok), QByteArray());
        // invalid bytearray
        QVERIFY(!ok);
        // The bytearray should be empty
        QVERIFY(prop.to_byte_array().isEmpty());
        // The bytearray should be not null
        QVERIFY(prop.to_byte_array().isNull());
        QVERIFY(!prop.value<const char*>());
        QCOMPARE(
            QByteArray(base::x11::xcb::string_property(connection, testWindow, XCB_ATOM_WM_NAME)),
            QByteArray());
    }

    SECTION("property bool")
    {
        base::x11::xcb::window testWindow(connection, createWindow());
        base::x11::xcb::atom blockCompositing(QByteArrayLiteral("_KDE_NET_WM_BLOCK_COMPOSITING"),
                                              connection);
        QVERIFY(blockCompositing != XCB_ATOM_NONE);
        win::x11::net::win_info info(connection,
                                     testWindow,
                                     root_window,
                                     win::x11::net::Properties(),
                                     win::x11::net::WM2BlockCompositing);

        base::x11::xcb::property prop(
            connection, false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
        bool ok = true;
        QVERIFY(!prop.to_bool());
        QVERIFY(!prop.to_bool(&ok));
        QVERIFY(!ok);

        info.setBlockingCompositing(true);

        xcb_flush(connection);
        prop = base::x11::xcb::property(
            connection, false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
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
            connection, false, testWindow, blockCompositing, XCB_ATOM_CARDINAL, 0, 100000);
        QVERIFY(!prop.to_bool());
        ok = true;
        QVERIFY(!prop.to_bool(&ok));
        QVERIFY(!ok);
    }

    SECTION("atom")
    {
        base::x11::xcb::atom atom(QByteArrayLiteral("WM_CLIENT_MACHINE"), connection);
        QCOMPARE(atom.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));
        QVERIFY(atom == XCB_ATOM_WM_CLIENT_MACHINE);
        QVERIFY(atom.is_valid());

        // test the const paths
        base::x11::xcb::atom const& atom2(atom);
        QVERIFY(atom2.is_valid());
        QVERIFY(atom2 == XCB_ATOM_WM_CLIENT_MACHINE);
        QCOMPARE(atom2.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));

        // destroy before retrieved
        base::x11::xcb::atom atom3(QByteArrayLiteral("WM_CLIENT_MACHINE"), connection);
        QCOMPARE(atom3.name(), QByteArrayLiteral("WM_CLIENT_MACHINE"));
    }

    SECTION("motif empty")
    {
        base::x11::xcb::atom atom(QByteArrayLiteral("_MOTIF_WM_HINTS"), connection);
        base::x11::xcb::motif_hints hints(connection, atom);

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

    SECTION("motif")
    {
        struct data {
            quint32 flags;
            quint32 functions;
            quint32 decorations;

            bool expectedHasDecoration;
            bool expectedNoBorder;
            bool expectedResize;
            bool expectedMove;
            bool expectedMinimize;
            bool expectedMaximize;
            bool expectedClose;
        };

        auto test_data
            = GENERATE(data{0u, 0u, 0u, false, false, true, true, true, true, true},
                       data{2u, 5u, 0u, true, true, true, true, true, true, true},
                       data{2u, 5u, 1u, true, false, true, true, true, true, true},
                       data{1u, 2u, 1u, false, false, true, false, false, false, false},
                       data{1u, 4u, 1u, false, false, false, true, false, false, false},
                       data{1u, 8u, 1u, false, false, false, false, true, false, false},
                       data{1u, 16u, 1u, false, false, false, false, false, true, false},
                       data{1u, 32u, 1u, false, false, false, false, false, false, true},
                       data{1u, 3u, 1u, false, false, false, true, true, true, true},
                       data{1u, 5u, 1u, false, false, true, false, true, true, true},
                       data{1u, 9u, 1u, false, false, true, true, false, true, true},
                       data{1u, 17u, 1u, false, false, true, true, true, false, true},
                       data{1u, 33u, 1u, false, false, true, true, true, true, false},
                       data{1u, 62u, 1u, false, false, true, true, true, true, true},
                       data{1u, 63u, 1u, false, false, false, false, false, false, false},
                       data{3u, 63u, 1u, true, false, false, false, false, false, false});

        base::x11::xcb::atom atom(QByteArrayLiteral("_MOTIF_WM_HINTS"), connection);

        quint32 data[] = {test_data.flags, test_data.functions, test_data.decorations, 0, 0};
        xcb_change_property(
            connection, XCB_PROP_MODE_REPLACE, m_testWindow, atom, atom, 32, 5, data);
        xcb_flush(connection);

        base::x11::xcb::motif_hints hints(connection, atom);
        hints.init(m_testWindow);
        hints.read();
        REQUIRE(hints.has_decoration() == test_data.expectedHasDecoration);
        REQUIRE(hints.no_border() == test_data.expectedNoBorder);
        REQUIRE(hints.resize() == test_data.expectedResize);
        REQUIRE(hints.move() == test_data.expectedMove);
        REQUIRE(hints.minimize() == test_data.expectedMinimize);
        REQUIRE(hints.maximize() == test_data.expectedMaximize);
        REQUIRE(hints.close() == test_data.expectedClose);
    }
}

}
