/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/control.h"
#include "win/deco.h"
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/space.h"
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

namespace KWin
{

class TransientPlacementTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testXdgPopup_data();
    void testXdgPopup();
    void testXdgPopupWithPanel();
};

void TransientPlacementTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

void TransientPlacementTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration
                                   | Test::global_selection::plasma_shell);

    Test::cursor()->set_pos(QPoint(640, 512));
}

void TransientPlacementTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void TransientPlacementTest::testXdgPopup_data()
{
    using namespace Wrapland::Client;

    QTest::addColumn<QSize>("parentSize");
    QTest::addColumn<QPoint>("parentPosition");
    QTest::addColumn<Wrapland::Client::xdg_shell_positioner_data>("pos_data");
    QTest::addColumn<QRect>("expectedGeometry");

    // window in the middle, plenty of room either side: Changing anchor

    // parent window is 500,500, starting at 300,300, anchorRect is therefore between 350->750 in
    // both dirs
    xdg_shell_positioner_data pos_data;
    pos_data.size = QSize(200, 200);
    pos_data.anchor.rect = QRect(50, 50, 400, 400);
    pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;

    QTest::newRow("anchorCenter") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                  << QRect(550, 550, 200, 200);

    pos_data.anchor.edge = Qt::TopEdge | Qt::LeftEdge;
    QTest::newRow("anchorTopLeft")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(350, 350, 200, 200);
    pos_data.anchor.edge = Qt::TopEdge;
    QTest::newRow("anchorTop") << QSize(500, 500) << QPoint(300, 300) << pos_data
                               << QRect(550, 350, 200, 200);
    pos_data.anchor.edge = Qt::TopEdge | Qt::RightEdge;
    QTest::newRow("anchorTopRight")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(750, 350, 200, 200);
    pos_data.anchor.edge = Qt::RightEdge;
    QTest::newRow("anchorRight") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                 << QRect(750, 550, 200, 200);
    pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
    QTest::newRow("anchorBottomRight")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(750, 750, 200, 200);
    pos_data.anchor.edge = Qt::BottomEdge;
    QTest::newRow("anchorBottom") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                  << QRect(550, 750, 200, 200);
    pos_data.anchor.edge = Qt::BottomEdge | Qt::LeftEdge;
    QTest::newRow("anchorBottomLeft")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(350, 750, 200, 200);
    pos_data.anchor.edge = Qt::LeftEdge;
    QTest::newRow("anchorLeft") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                << QRect(350, 550, 200, 200);

    // ----------------------------------------------------------------
    // window in the middle, plenty of room either side: Changing gravity around the bottom right
    // anchor
    pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
    pos_data.gravity = Qt::Edges();
    QTest::newRow("gravityCentre")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(650, 650, 200, 200);
    pos_data.gravity = Qt::TopEdge | Qt::LeftEdge;
    QTest::newRow("gravityTopLeft")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(550, 550, 200, 200);
    pos_data.gravity = Qt::TopEdge;
    QTest::newRow("gravityTop") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                << QRect(650, 550, 200, 200);
    pos_data.gravity = Qt::TopEdge | Qt::RightEdge;
    QTest::newRow("gravityTopRight")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(750, 550, 200, 200);
    pos_data.gravity = Qt::RightEdge;
    QTest::newRow("gravityRight") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                  << QRect(750, 650, 200, 200);
    pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;
    QTest::newRow("gravityBottomRight")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(750, 750, 200, 200);
    pos_data.gravity = Qt::BottomEdge;
    QTest::newRow("gravityBottom")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(650, 750, 200, 200);
    pos_data.gravity = Qt::BottomEdge | Qt::LeftEdge;
    QTest::newRow("gravityBottomLeft")
        << QSize(500, 500) << QPoint(300, 300) << pos_data << QRect(550, 750, 200, 200);
    pos_data.gravity = Qt::LeftEdge;
    QTest::newRow("gravityLeft") << QSize(500, 500) << QPoint(300, 300) << pos_data
                                 << QRect(550, 650, 200, 200);

    // ----------------------------------------------------------------
    // constrain and slide
    // popup is still 200,200. window moved near edge of screen, popup always comes out towards the
    // screen edge
    pos_data.constraint_adjustments = QFlags(
        {xdg_shell_constraint_adjustment::slide_x, xdg_shell_constraint_adjustment::slide_y});

    pos_data.anchor.edge = Qt::TopEdge;
    pos_data.gravity = Qt::TopEdge;
    QTest::newRow("constraintSlideTop")
        << QSize(500, 500) << QPoint(80, 80) << pos_data << QRect(80 + 250 - 100, 0, 200, 200);

    pos_data.anchor.edge = Qt::LeftEdge;
    pos_data.gravity = Qt::LeftEdge;
    QTest::newRow("constraintSlideLeft")
        << QSize(500, 500) << QPoint(80, 80) << pos_data << QRect(0, 80 + 250 - 100, 200, 200);

    pos_data.anchor.edge = Qt::RightEdge;
    pos_data.gravity = Qt::RightEdge;
    QTest::newRow("constraintSlideRight") << QSize(500, 500) << QPoint(700, 80) << pos_data
                                          << QRect(1280 - 200, 80 + 250 - 100, 200, 200);

    pos_data.anchor.edge = Qt::BottomEdge;
    pos_data.gravity = Qt::BottomEdge;
    QTest::newRow("constraintSlideBottom") << QSize(500, 500) << QPoint(80, 500) << pos_data
                                           << QRect(80 + 250 - 100, 1024 - 200, 200, 200);

    pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
    pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;
    QTest::newRow("constraintSlideBottomRight") << QSize(500, 500) << QPoint(700, 1000) << pos_data
                                                << QRect(1280 - 200, 1024 - 200, 200, 200);

    // ----------------------------------------------------------------
    // constrain and flip
    pos_data.constraint_adjustments = QFlags(
        {xdg_shell_constraint_adjustment::flip_x, xdg_shell_constraint_adjustment::flip_y});

    pos_data.anchor.edge = Qt::TopEdge;
    pos_data.gravity = Qt::TopEdge;
    QTest::newRow("constraintFlipTop")
        << QSize(500, 500) << QPoint(80, 80) << pos_data << QRect(230, 80 + 500 - 50, 200, 200);

    pos_data.anchor.edge = Qt::LeftEdge;
    pos_data.gravity = Qt::LeftEdge;
    QTest::newRow("constraintFlipLeft")
        << QSize(500, 500) << QPoint(80, 80) << pos_data << QRect(80 + 500 - 50, 230, 200, 200);

    pos_data.anchor.edge = Qt::RightEdge;
    pos_data.gravity = Qt::RightEdge;
    QTest::newRow("constraintFlipRight")
        << QSize(500, 500) << QPoint(700, 80) << pos_data << QRect(700 + 50 - 200, 230, 200, 200);

    pos_data.anchor.edge = Qt::BottomEdge;
    pos_data.gravity = Qt::BottomEdge;
    QTest::newRow("constraintFlipBottom")
        << QSize(500, 500) << QPoint(80, 500) << pos_data << QRect(230, 500 + 50 - 200, 200, 200);

    pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
    pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;
    QTest::newRow("constraintFlipBottomRight") << QSize(500, 500) << QPoint(700, 500) << pos_data
                                               << QRect(700 + 50 - 200, 500 + 50 - 200, 200, 200);

    pos_data.anchor.edge = Qt::TopEdge;
    pos_data.gravity = Qt::RightEdge;
    // as popup is positioned in the middle of the parent we need a massive popup to be able to
    // overflow
    pos_data.size = QSize(400, 400);
    QTest::newRow("constraintFlipRightNoAnchor")
        << QSize(500, 500) << QPoint(700, 80) << pos_data << QRect(700 + 250 - 400, 330, 400, 400);

    pos_data.anchor.edge = Qt::RightEdge;
    pos_data.gravity = Qt::TopEdge;
    pos_data.size = QSize(300, 200);
    QTest::newRow("constraintFlipRightNoGravity")
        << QSize(500, 500) << QPoint(700, 80) << pos_data << QRect(700 + 50 - 150, 130, 300, 200);

    // ----------------------------------------------------------------
    // resize
    pos_data.constraint_adjustments = QFlags(
        {xdg_shell_constraint_adjustment::resize_x, xdg_shell_constraint_adjustment::resize_y});
    pos_data.size = QSize(200, 200);

    pos_data.anchor.edge = Qt::TopEdge;
    pos_data.gravity = Qt::TopEdge;
    QTest::newRow("resizeTop") << QSize(500, 500) << QPoint(80, 80) << pos_data
                               << QRect(80 + 250 - 100, 0, 200, 130);

    pos_data.anchor.edge = Qt::LeftEdge;
    pos_data.gravity = Qt::LeftEdge;
    QTest::newRow("resizeLeft") << QSize(500, 500) << QPoint(80, 80) << pos_data
                                << QRect(0, 80 + 250 - 100, 130, 200);

    pos_data.anchor.edge = Qt::RightEdge;
    pos_data.gravity = Qt::RightEdge;
    QTest::newRow("resizeRight") << QSize(500, 500) << QPoint(700, 80) << pos_data
                                 << QRect(700 + 50 + 400, 80 + 250 - 100, 130, 200);

    pos_data.anchor.edge = Qt::BottomEdge;
    pos_data.gravity = Qt::BottomEdge;
    QTest::newRow("resizeBottom") << QSize(500, 500) << QPoint(80, 500) << pos_data
                                  << QRect(80 + 250 - 100, 500 + 50 + 400, 200, 74);
}

void TransientPlacementTest::testXdgPopup()
{
    using namespace Wrapland::Client;

    // this test verifies that the position of a transient window is taken from the passed position
    // there are no further constraints like window too large to fit screen, cascading transients,
    // etc some test cases also verify that the transient fits on the screen
    QFETCH(QSize, parentSize);
    QFETCH(QPoint, parentPosition);
    QFETCH(QRect, expectedGeometry);
    const QRect expectedRelativeGeometry = expectedGeometry.translated(-parentPosition);

    auto surface = std::unique_ptr<Wrapland::Client::Surface>(Test::create_surface());
    QVERIFY(surface);
    auto parentShellSurface = std::unique_ptr<Wrapland::Client::XdgShellToplevel>(
        Test::create_xdg_shell_toplevel(surface));
    QVERIFY(parentShellSurface);
    auto parent = Test::render_and_wait_for_shown(surface, parentSize, Qt::blue);
    QVERIFY(parent);

    QVERIFY(!win::decoration(parent));
    win::move(parent, parentPosition);
    QCOMPARE(parent->geo.frame, QRect(parentPosition, parentSize));

    // create popup
    QFETCH(xdg_shell_positioner_data, pos_data);

    auto transientSurface = std::unique_ptr<Wrapland::Client::Surface>(Test::create_surface());
    QVERIFY(transientSurface);

    auto popup = Test::create_xdg_shell_popup(
        transientSurface, parentShellSurface, pos_data, Test::CreationSetup::CreateOnly);
    QSignalSpy configureRequestedSpy(popup.get(), &XdgShellPopup::configureRequested);
    transientSurface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.first()[0].value<QRect>(), expectedRelativeGeometry);
    popup->ackConfigure(configureRequestedSpy.first()[1].toUInt());

    auto transient = Test::render_and_wait_for_shown(
        transientSurface, expectedRelativeGeometry.size(), Qt::red);
    QVERIFY(transient);

    QVERIFY(!win::decoration(transient));
    QCOMPARE(transient->geo.frame, expectedGeometry);

    QCOMPARE(configureRequestedSpy.count(), 1); // check that we did not get reconfigured
}

void TransientPlacementTest::testXdgPopupWithPanel()
{
    // Ensures that an xdg-popup is placed with respect to panels, i.e. the placement area.

    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface{Test::create_surface()};
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> dockShellSurface{Test::create_xdg_shell_toplevel(surface)};
    QVERIFY(dockShellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);

    // Put the panel at the lower screen border.
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaSurface->setPosition(QPoint(0, Test::get_output(0)->geometry().height() - 50));
    plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);

    // Placement area still full screen.
    QVERIFY(win::space_window_area(*Test::app()->base->space, PlacementArea, 0, 1)
            == win::space_window_area(*Test::app()->base->space, FullScreenArea, 0, 1));

    // Now map the panel and placement area is reduced.
    auto dock = Test::render_and_wait_for_shown(surface, QSize(1280, 50), Qt::blue);
    QVERIFY(dock);
    QCOMPARE(dock->windowType(), win::win_type::dock);
    QVERIFY(win::is_dock(dock));
    QCOMPARE(dock->geo.frame, QRect(0, Test::get_output(0)->geometry().height() - 50, 1280, 50));
    QCOMPARE(dock->hasStrut(), true);
    QVERIFY(win::space_window_area(*Test::app()->base->space, PlacementArea, 0, 1)
            != win::space_window_area(*Test::app()->base->space, FullScreenArea, 0, 1));

    // Create parent
    auto parentSurface = Test::create_surface();
    QVERIFY(parentSurface);
    auto parentShellSurface = Test::create_xdg_shell_toplevel(parentSurface);
    QVERIFY(parentShellSurface);
    auto parent = Test::render_and_wait_for_shown(parentSurface, {800, 600}, Qt::blue);
    QVERIFY(parent);

    QVERIFY(!win::decoration(parent));

    win::move(parent, {0, Test::get_output(0)->geometry().height() - 300});
    win::keep_in_area(
        parent, win::space_window_area(*Test::app()->base->space, PlacementArea, parent), false);
    QCOMPARE(parent->geo.frame,
             QRect(0, Test::get_output(0)->geometry().height() - 600 - 50, 800, 600));

    auto transientSurface = Test::create_surface();
    QVERIFY(transientSurface);

    xdg_shell_positioner_data pos_data;
    pos_data.size = QSize(200, 200);
    pos_data.anchor.rect = QRect(50, 500, 200, 200);
    pos_data.constraint_adjustments = xdg_shell_constraint_adjustment::slide_y;

    auto transientShellSurface
        = Test::create_xdg_shell_popup(transientSurface, parentShellSurface, pos_data);
    auto transient = Test::render_and_wait_for_shown(transientSurface, pos_data.size, Qt::red);
    QVERIFY(transient);

    QVERIFY(!win::decoration(transient));
    QCOMPARE(transient->geo.frame,
             QRect(50, Test::get_output(0)->geometry().height() - 200 - 50, 200, 200));

    transientShellSurface.reset();
    transientSurface.reset();
    QVERIFY(Test::wait_for_destroyed(transient));

    // now parent to fullscreen - on fullscreen the panel is ignored
    QSignalSpy fullscreenSpy{parentShellSurface.get(), &XdgShellToplevel::configured};
    QVERIFY(fullscreenSpy.isValid());
    parent->setFullScreen(true);
    QVERIFY(fullscreenSpy.wait());
    parentShellSurface->ackConfigure(fullscreenSpy.front().back().value<quint32>());
    QSignalSpy geometryShapeChangedSpy{parent->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed};
    QVERIFY(geometryShapeChangedSpy.isValid());

    Test::render(parentSurface, parentShellSurface->get_configure_data().size, Qt::red);
    QVERIFY(geometryShapeChangedSpy.wait());
    QCOMPARE(parent->geo.frame, Test::get_output(0)->geometry());
    QVERIFY(parent->control->fullscreen);

    // another transient, with same hints as before from bottom of window
    transientSurface = Test::create_surface();
    QVERIFY(transientSurface);

    xdg_shell_positioner_data pos_data2;
    pos_data2.size = QSize(200, 200);
    pos_data2.anchor.rect = QRect(50, Test::get_output(0)->geometry().height() - 100, 200, 200);
    pos_data2.constraint_adjustments = xdg_shell_constraint_adjustment::slide_y;

    transientShellSurface
        = Test::create_xdg_shell_popup(transientSurface, parentShellSurface, pos_data2);
    QVERIFY(transientShellSurface);
    transient = Test::render_and_wait_for_shown(transientSurface, pos_data2.size, Qt::red);
    QVERIFY(transient);

    QVERIFY(!win::decoration(transient));
    QCOMPARE(transient->geo.frame,
             QRect(50, Test::get_output(0)->geometry().height() - 200, 200, 200));
}

}

WAYLANDTEST_MAIN(KWin::TransientPlacementTest)
#include "transient_placement.moc"
