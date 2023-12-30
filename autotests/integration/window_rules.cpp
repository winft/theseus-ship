/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

TEST_CASE("window rules", "[win]")
{
    test::setup setup("window-rules", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();
    cursor()->set_pos(QPoint(640, 512));

    auto get_x11_window_from_id
        = [&](uint32_t id) { return get_x11_window(setup.base->mod.space->windows_map.at(id)); };

    auto get_config = [&]() -> std::tuple<KSharedConfigPtr, KConfigGroup> {
        auto config = setup.base->config.main;

        auto group = config->group(QStringLiteral("1"));
        group.deleteGroup();
        config->group(QStringLiteral("General")).writeEntry("count", 1);
        return {config, group};
    };

    SECTION("apply initial maximize vert")
    {
        // this test creates the situation of BUG 367554: creates a window and initial apply
        // maximize vertical the window is matched by class and role load the rule

        auto role = GENERATE(std::string("mainwindow"), std::string("MainWindow"));

        auto [config, group] = get_config();
        group.writeEntry("maximizevert", true);
        group.writeEntry("maximizevertrule", 3);
        group.writeEntry("title", "KPatience");
        group.writeEntry("titlematch", 0);
        group.writeEntry("types", 1);
        group.writeEntry("windowrole", "mainwindow");
        group.writeEntry("windowrolematch", 1);
        group.writeEntry("clientmachine", "localhost");
        group.writeEntry("clientmachinematch", 0);
        group.writeEntry("wmclass", "kpat");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", enum_index(win::rules::name_match::exact));
        group.sync();
        setup.base->mod.space->rule_book->config = config;
        win::space_reconfigure(*setup.base->mod.space);

        // create the test window
        auto c = xcb_connection_create();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry = QRect(0, 0, 10, 20);
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
        xcb_icccm_set_wm_class(c.get(), w, 9, "kpat\0kpat");

        xcb_change_property(c.get(),
                            XCB_PROP_MODE_REPLACE,
                            w,
                            setup.base->mod.space->atoms->wm_window_role,
                            XCB_ATOM_STRING,
                            8,
                            role.size(),
                            role.c_str());

        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::normal);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.last().first().value<quint32>());
        QVERIFY(client);
        QVERIFY(win::decoration(client));
        QVERIFY(!client->hasStrut());
        QVERIFY(!client->isHiddenInternal());
        QTRY_VERIFY(client->render_data.ready_for_painting);
        if (!client->surface) {
            QSignalSpy surfaceChangedSpy(client->qobject.get(),
                                         &win::window_qobject::surfaceChanged);
            QVERIFY(surfaceChangedSpy.isValid());
            QVERIFY(surfaceChangedSpy.wait());
        }
        QVERIFY(client->surface);
        QCOMPARE(client->maximizeMode(), win::maximize_mode::vertical);

        // destroy window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("window class change")
    {
        auto [config, group] = get_config();

        group.writeEntry("above", true);
        group.writeEntry("aboverule", 2);
        group.writeEntry("wmclass", "org.kde.foo");
        group.writeEntry("wmclasscomplete", false);
        group.writeEntry("wmclassmatch", 1);
        group.sync();

        setup.base->mod.space->rule_book->config = config;
        win::space_reconfigure(*setup.base->mod.space);

        // create the test window
        auto c = xcb_connection_create();
        QVERIFY(!xcb_connection_has_error(c.get()));

        xcb_window_t w = xcb_generate_id(c.get());
        const QRect windowGeometry = QRect(0, 0, 10, 20);
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
        xcb_icccm_set_wm_class(c.get(), w, 23, "org.kde.bar\0org.kde.bar");

        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::normal);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.last().first().value<quint32>());
        QVERIFY(client);
        QVERIFY(win::decoration(client));
        QVERIFY(!client->hasStrut());
        QVERIFY(!client->isHiddenInternal());
        QVERIFY(!client->render_data.ready_for_painting);
        QTRY_VERIFY(client->render_data.ready_for_painting);
        if (!client->surface) {
            QSignalSpy surfaceChangedSpy(client->qobject.get(),
                                         &win::window_qobject::surfaceChanged);
            QVERIFY(surfaceChangedSpy.isValid());
            QVERIFY(surfaceChangedSpy.wait());
        }
        QVERIFY(client->surface);
        QCOMPARE(client->control->keep_above, false);

        // now change class
        QSignalSpy windowClassChangedSpy{client->qobject.get(),
                                         &win::window_qobject::windowClassChanged};
        QVERIFY(windowClassChangedSpy.isValid());
        xcb_icccm_set_wm_class(c.get(), w, 23, "org.kde.foo\0org.kde.foo");
        xcb_flush(c.get());
        QVERIFY(windowClassChangedSpy.wait());
        QCOMPARE(client->control->keep_above, true);

        // destroy window
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }
}

}
