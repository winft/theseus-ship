/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/control.h"
#include "win/deco.h"
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("transient placement", "[win]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("transient-placement", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(640, 512));
    setup_wayland_connection(global_selection::xdg_decoration | global_selection::plasma_shell);

    SECTION("xdg popup")
    {
        using namespace Wrapland::Client;

        struct data {
            QSize parent_size;
            QPoint parent_pos;
            Wrapland::Client::xdg_shell_positioner_data pos_data;
            QRect expected_geo;
        };

        // parent window is 500,500, starting at 300,300, anchorRect is therefore between 350->750
        // in both dirs
        auto test_data = GENERATE(
            // anchorCenter
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = {50, 50, 400, 400},},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {550, 550, 200, 200},
            },
            // anchorTopLeft
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge | Qt::LeftEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {350, 350, 200, 200},
            },
            // anchorTop
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {550, 350, 200, 200},
            },
            // anchorTopRight
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge | Qt::RightEdge,
                 },
                 {750, 350, 200, 200}},
            // anchorRight
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::RightEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {750, 550, 200, 200},
            },
            // anchorBottomRight
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor
                    = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {750, 750, 200, 200},
            },
            // anchorBottom
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {550, 750, 200, 200},
            },
            // anchorBottomLeft
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor
                    = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::LeftEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {350, 750, 200, 200},
            },
            // anchorLeft
            data{
                {500, 500},
                {300, 300},
                {
                    .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::LeftEdge},
                    .size = {200, 200},
                    .gravity = Qt::BottomEdge | Qt::RightEdge,
                },
                {350, 550, 200, 200},
            },
            // ----------------------------------------------------------------
            // window in the middle, plenty of room either side: Changing gravity around the bottom
            // right anchor gravityCentre
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = {},
                 },
                 {650, 650, 200, 200}},
            // gravityTopLeft
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge | Qt::LeftEdge,
                 },
                 {550, 550, 200, 200}},
            // gravityTop
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge,
                 },
                 {650, 550, 200, 200}},
            // gravityTopRight
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge | Qt::RightEdge,
                 },
                 {750, 550, 200, 200}},
            // gravityRight
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::RightEdge,
                 },
                 {750, 650, 200, 200}},
            // gravityBottomRight
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge | Qt::RightEdge,
                 },
                 {750, 750, 200, 200}},
            // gravityBottom
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge,
                 },
                 {650, 750, 200, 200}},
            // gravityBottomLeft
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge | Qt::LeftEdge,
                 },
                 {550, 750, 200, 200}},
            // gravityLeft
            data{{500, 500},
                 {300, 300},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::LeftEdge,
                 },
                 {550, 650, 200, 200}},
            // ----------------------------------------------------------------
            // constrain and slide
            // popup is still 200,200. window moved near edge of screen, popup always comes out
            // towards the screen edge constraintSlideTop
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::slide_x,
                                                       xdg_shell_constraint_adjustment::slide_y}),
                 },
                 {80 + 250 - 100, 0, 200, 200}},
            // constraintSlideLeft
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::LeftEdge},
                     .size = {200, 200},
                     .gravity = Qt::LeftEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::slide_x,
                                                       xdg_shell_constraint_adjustment::slide_y}),
                 },
                 {0, 80 + 250 - 100, 200, 200}},
            // constraintSlideRight
            data{{500, 500},
                 {700, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::slide_x,
                                                       xdg_shell_constraint_adjustment::slide_y}),
                 },
                 {1280 - 200, 80 + 250 - 100, 200, 200}},
            // constraintSlideBottom
            data{{500, 500},
                 {80, 500},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::slide_x,
                                                       xdg_shell_constraint_adjustment::slide_y}),
                 },
                 {80 + 250 - 100, 1024 - 200, 200, 200}},
            // constraintSlideBottomRight
            data{{500, 500},
                 {700, 1000},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge | Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::slide_x,
                                                       xdg_shell_constraint_adjustment::slide_y}),
                 },
                 {1280 - 200, 1024 - 200, 200, 200}},
            // ----------------------------------------------------------------
            // constrain and flip
            // constraintFlipTop
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {230, 80 + 500 - 50, 200, 200}},
            // constraintFlipLeft
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::LeftEdge},
                     .size = {200, 200},
                     .gravity = Qt::LeftEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {80 + 500 - 50, 230, 200, 200}},
            // constraintFlipRight
            data{{500, 500},
                 {700, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {700 + 50 - 200, 230, 200, 200}},
            // constraintFlipBottom
            data{{500, 500},
                 {80, 500},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {230, 500 + 50 - 200, 200, 200}},
            // constraintFlipBottomRight
            data{{500, 500},
                 {700, 500},
                 {
                     .anchor
                     = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge | Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge | Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {700 + 50 - 200, 500 + 50 - 200, 200, 200}},
            // constraintFlipRightNoAnchor
            // as popup is positioned in the middle of the parent we need a massive popup to be able
            // to overflow
            data{{500, 500},
                 {700, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge},
                     .size = {400, 400},
                     .gravity = Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {700 + 250 - 400, 330, 400, 400}},
            // constraintFlipRightNoGravity
            data{{500, 500},
                 {700, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::RightEdge},
                     .size = {300, 200},
                     .gravity = Qt::TopEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::flip_x,
                                                       xdg_shell_constraint_adjustment::flip_y}),
                 },
                 {700 + 50 - 150, 130, 300, 200}},
            // ----------------------------------------------------------------
            // resize
            // resizeTop
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::TopEdge},
                     .size = {200, 200},
                     .gravity = Qt::TopEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::resize_x,
                                                       xdg_shell_constraint_adjustment::resize_y}),
                 },
                 {80 + 250 - 100, 0, 200, 130}},
            // resizeLeft
            data{{500, 500},
                 {80, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::LeftEdge},
                     .size = {200, 200},
                     .gravity = Qt::LeftEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::resize_x,
                                                       xdg_shell_constraint_adjustment::resize_y}),
                 },
                 {0, 80 + 250 - 100, 130, 200}},
            // resizeRight
            data{{500, 500},
                 {700, 80},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::RightEdge},
                     .size = {200, 200},
                     .gravity = Qt::RightEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::resize_x,
                                                       xdg_shell_constraint_adjustment::resize_y}),
                 },
                 {700 + 50 + 400, 80 + 250 - 100, 130, 200}},
            // resizeBottom
            data{{500, 500},
                 {80, 500},
                 {
                     .anchor = {.rect = QRect(50, 50, 400, 400), .edge = Qt::BottomEdge},
                     .size = {200, 200},
                     .gravity = Qt::BottomEdge,
                     .constraint_adjustments = QFlags({xdg_shell_constraint_adjustment::resize_x,
                                                       xdg_shell_constraint_adjustment::resize_y}),
                 },
                 {80 + 250 - 100, 500 + 50 + 400, 200, 74}});

        // this test verifies that the position of a transient window is taken from the passed
        // position there are no further constraints like window too large to fit screen, cascading
        // transients, etc some test cases also verify that the transient fits on the screen
        const QRect expectedRelativeGeometry
            = test_data.expected_geo.translated(-test_data.parent_pos);

        auto surface = std::unique_ptr<Wrapland::Client::Surface>(create_surface());
        QVERIFY(surface);
        auto parentShellSurface = std::unique_ptr<Wrapland::Client::XdgShellToplevel>(
            create_xdg_shell_toplevel(surface));
        QVERIFY(parentShellSurface);
        auto parent = render_and_wait_for_shown(surface, test_data.parent_size, Qt::blue);
        QVERIFY(parent);

        QVERIFY(!win::decoration(parent));
        win::move(parent, test_data.parent_pos);
        QCOMPARE(parent->geo.frame, QRect(test_data.parent_pos, test_data.parent_size));

        // create popup
        auto transientSurface = std::unique_ptr<Wrapland::Client::Surface>(create_surface());
        QVERIFY(transientSurface);

        auto popup = create_xdg_shell_popup(
            transientSurface, parentShellSurface, test_data.pos_data, CreationSetup::CreateOnly);
        QSignalSpy configureRequestedSpy(popup.get(), &XdgShellPopup::configureRequested);
        transientSurface->commit(Surface::CommitFlag::None);

        configureRequestedSpy.wait();
        QCOMPARE(configureRequestedSpy.count(), 1);
        QCOMPARE(configureRequestedSpy.first()[0].value<QRect>(), expectedRelativeGeometry);
        popup->ackConfigure(configureRequestedSpy.first()[1].toUInt());

        auto transient
            = render_and_wait_for_shown(transientSurface, expectedRelativeGeometry.size(), Qt::red);
        QVERIFY(transient);

        QVERIFY(!win::decoration(transient));
        QCOMPARE(transient->geo.frame, test_data.expected_geo);

        QCOMPARE(configureRequestedSpy.count(), 1); // check that we did not get reconfigured
    }

    SECTION("xdg popup with panel")
    {
        // Ensures that an xdg-popup is placed with respect to panels, i.e. the placement area.

        using namespace Wrapland::Client;

        std::unique_ptr<Surface> surface{create_surface()};
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> dockShellSurface{create_xdg_shell_toplevel(surface)};
        QVERIFY(dockShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(surface.get()));
        QVERIFY(plasmaSurface);

        // Put the panel at the lower screen border.
        plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaSurface->setPosition(QPoint(0, get_output(0)->geometry().height() - 50));
        plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);

        // Placement area still full screen.
        QVERIFY(win::space_window_area(*setup.base->space, win::area_option::placement, 0, 1)
                == win::space_window_area(*setup.base->space, win::area_option::fullscreen, 0, 1));

        // Now map the panel and placement area is reduced.
        auto dock = render_and_wait_for_shown(surface, QSize(1280, 50), Qt::blue);
        QVERIFY(dock);
        QCOMPARE(dock->windowType(), win::win_type::dock);
        QVERIFY(win::is_dock(dock));
        QCOMPARE(dock->geo.frame, QRect(0, get_output(0)->geometry().height() - 50, 1280, 50));
        QCOMPARE(dock->hasStrut(), true);
        QVERIFY(win::space_window_area(*setup.base->space, win::area_option::placement, 0, 1)
                != win::space_window_area(*setup.base->space, win::area_option::fullscreen, 0, 1));

        // Create parent
        auto parentSurface = create_surface();
        QVERIFY(parentSurface);
        auto parentShellSurface = create_xdg_shell_toplevel(parentSurface);
        QVERIFY(parentShellSurface);
        auto parent = render_and_wait_for_shown(parentSurface, {800, 600}, Qt::blue);
        QVERIFY(parent);

        QVERIFY(!win::decoration(parent));

        win::move(parent, {0, get_output(0)->geometry().height() - 300});
        win::keep_in_area(
            parent,
            win::space_window_area(*setup.base->space, win::area_option::placement, parent),
            false);
        QCOMPARE(parent->geo.frame,
                 QRect(0, get_output(0)->geometry().height() - 600 - 50, 800, 600));

        auto transientSurface = create_surface();
        QVERIFY(transientSurface);

        xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(200, 200);
        pos_data.anchor.rect = QRect(50, 500, 200, 200);
        pos_data.constraint_adjustments = xdg_shell_constraint_adjustment::slide_y;

        auto transientShellSurface
            = create_xdg_shell_popup(transientSurface, parentShellSurface, pos_data);
        auto transient = render_and_wait_for_shown(transientSurface, pos_data.size, Qt::red);
        QVERIFY(transient);

        QVERIFY(!win::decoration(transient));
        QCOMPARE(transient->geo.frame,
                 QRect(50, get_output(0)->geometry().height() - 200 - 50, 200, 200));

        transientShellSurface.reset();
        transientSurface.reset();
        QVERIFY(wait_for_destroyed(transient));

        // now parent to fullscreen - on fullscreen the panel is ignored
        QSignalSpy fullscreenSpy{parentShellSurface.get(), &XdgShellToplevel::configured};
        QVERIFY(fullscreenSpy.isValid());
        parent->setFullScreen(true);
        QVERIFY(fullscreenSpy.wait());
        parentShellSurface->ackConfigure(fullscreenSpy.front().back().value<quint32>());
        QSignalSpy geometryShapeChangedSpy{parent->qobject.get(),
                                           &win::window_qobject::frame_geometry_changed};
        QVERIFY(geometryShapeChangedSpy.isValid());

        render(parentSurface, parentShellSurface->get_configure_data().size, Qt::red);
        QVERIFY(geometryShapeChangedSpy.wait());
        QCOMPARE(parent->geo.frame, get_output(0)->geometry());
        QVERIFY(parent->control->fullscreen);

        // another transient, with same hints as before from bottom of window
        transientSurface = create_surface();
        QVERIFY(transientSurface);

        xdg_shell_positioner_data pos_data2;
        pos_data2.size = QSize(200, 200);
        pos_data2.anchor.rect = QRect(50, get_output(0)->geometry().height() - 100, 200, 200);
        pos_data2.constraint_adjustments = xdg_shell_constraint_adjustment::slide_y;

        transientShellSurface
            = create_xdg_shell_popup(transientSurface, parentShellSurface, pos_data2);
        QVERIFY(transientShellSurface);
        transient = render_and_wait_for_shown(transientSurface, pos_data2.size, Qt::red);
        QVERIFY(transient);

        QVERIFY(!win::decoration(transient));
        QCOMPARE(transient->geo.frame,
                 QRect(50, get_output(0)->geometry().height() - 200, 200, 200));
    }
}

}
