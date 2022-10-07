/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "main.h"
#include "toplevel.h"
#include "utils/blocker.h"
#include "win/actions.h"
#include "win/activation.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class StackingOrderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testTransientIsAboveParent();
    void testRaiseTransient();
    void testDeletedTransient();

    void testGroupTransientIsAboveWindowGroup();
    void testRaiseGroupTransient();
    void testDeletedGroupTransient();
    void testDontKeepAboveNonModalDialogGroupTransients();

    void testKeepAbove();
    void testKeepBelow();
};

Test::space::x11_window* get_x11_window_from_id(uint32_t id)
{
    return Test::get_x11_window(Test::app()->base.space->windows_map.at(id));
}

void StackingOrderTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void StackingOrderTest::init()
{
    Test::setup_wayland_connection();
}

void StackingOrderTest::cleanup()
{
    Test::destroy_wayland_connection();
    QTRY_VERIFY(Test::app()->base.space->stacking.order.stack.empty());
}

template<typename Win>
void deleted_deleter(Win* deleted)
{
    if (deleted != nullptr) {
        QCOMPARE(deleted->remnant->refcount, 1);
        deleted->remnant->unref();
    }
}

template<typename Win>
std::unique_ptr<Win, void (*)(Win*)> create_deleted(Test::space::window_t deleted)
{
    return {std::get<Win*>(deleted), deleted_deleter<Win>};
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

static xcb_window_t createGroupWindow(xcb_connection_t* conn,
                                      const QRect& geometry,
                                      xcb_window_t leaderWid = XCB_WINDOW_NONE)
{
    xcb_window_t wid = xcb_generate_id(conn);
    xcb_create_window(conn,                          // c
                      XCB_COPY_FROM_PARENT,          // depth
                      wid,                           // wid
                      rootWindow(),                  // parent
                      geometry.x(),                  // x
                      geometry.y(),                  // y
                      geometry.width(),              // width
                      geometry.height(),             // height
                      0,                             // border_width
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, // _class
                      XCB_COPY_FROM_PARENT,          // visual
                      0,                             // value_mask
                      nullptr                        // value_list
    );

    xcb_size_hints_t sizeHints = {};
    xcb_icccm_size_hints_set_position(&sizeHints, 1, geometry.x(), geometry.y());
    xcb_icccm_size_hints_set_size(&sizeHints, 1, geometry.width(), geometry.height());
    xcb_icccm_set_wm_normal_hints(conn, wid, &sizeHints);

    if (leaderWid == XCB_WINDOW_NONE) {
        leaderWid = wid;
    }

    xcb_change_property(conn,                                             // c
                        XCB_PROP_MODE_REPLACE,                            // mode
                        wid,                                              // window
                        Test::app()->base.space->atoms->wm_client_leader, // property
                        XCB_ATOM_WINDOW,                                  // type
                        32,                                               // format
                        1,                                                // data_len
                        &leaderWid                                        // data
    );

    return wid;
}

void StackingOrderTest::testTransientIsAboveParent()
{
    // This test verifies that transients are always above their parents.

    // Create the parent.
    auto parentSurface = std::unique_ptr<Wrapland::Client::Surface>(Test::create_surface());
    QVERIFY(parentSurface);
    auto parentShellSurface = Test::create_xdg_shell_toplevel(parentSurface);
    QVERIFY(parentShellSurface);
    auto parent = Test::render_and_wait_for_shown(parentSurface, QSize(256, 256), Qt::blue);
    QVERIFY(parent);
    QVERIFY(parent->control->active);
    QVERIFY(!parent->transient->lead());

    // Initially, the stacking order should contain only the parent window.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent}));

    // Create the transient.
    auto transientSurface = Test::create_surface();
    QVERIFY(transientSurface);
    auto transientShellSurface = Test::create_xdg_shell_toplevel(transientSurface);
    QVERIFY(transientShellSurface);
    transientShellSurface->setTransientFor(parentShellSurface.get());
    auto transient = Test::render_and_wait_for_shown(transientSurface, QSize(128, 128), Qt::red);
    QVERIFY(transient);
    QVERIFY(transient->control->active);
    QVERIFY(transient->transient->lead());

    // The transient should be above the parent.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient}));

    // The transient still stays above the parent if we activate the latter.
    win::activate_window(*Test::app()->base.space, *parent);
    QTRY_VERIFY(parent->control->active);
    QTRY_VERIFY(!transient->control->active);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient}));
}

void StackingOrderTest::testRaiseTransient()
{
    // This test verifies that both the parent and the transient will be
    // raised if either one of them is activated.

    // Create the parent.
    auto parentSurface = Test::create_surface();
    QVERIFY(parentSurface);
    auto parentShellSurface = Test::create_xdg_shell_toplevel(parentSurface);
    QVERIFY(parentShellSurface);
    auto parent = Test::render_and_wait_for_shown(parentSurface, QSize(256, 256), Qt::blue);
    QVERIFY(parent);
    QVERIFY(parent->control->active);
    QVERIFY(!parent->transient->lead());

    // Initially, the stacking order should contain only the parent window.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent}));

    // Create the transient.
    auto transientSurface = Test::create_surface();
    QVERIFY(transientSurface);
    auto transientShellSurface = Test::create_xdg_shell_toplevel(transientSurface);
    QVERIFY(transientShellSurface);
    transientShellSurface->setTransientFor(parentShellSurface.get());
    auto transient = Test::render_and_wait_for_shown(transientSurface, QSize(128, 128), Qt::red);
    QVERIFY(transient);
    QTRY_VERIFY(transient->control->active);
    QVERIFY(transient->transient->lead());

    // The transient should be above the parent.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient}));

    // Create a window that doesn't have any relationship to the parent or the transient.
    auto anotherSurface = Test::create_surface();
    QVERIFY(anotherSurface);
    auto anotherShellSurface = Test::create_xdg_shell_toplevel(anotherSurface);
    QVERIFY(anotherShellSurface);
    auto anotherClient
        = Test::render_and_wait_for_shown(anotherSurface, QSize(128, 128), Qt::green);
    QVERIFY(anotherClient);
    QVERIFY(anotherClient->control->active);
    QVERIFY(!anotherClient->transient->lead());

    // The newly created surface has to be above both the parent and the transient.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient, anotherClient}));

    // If we activate the parent, the transient should be raised too.
    win::activate_window(*Test::app()->base.space, *parent);
    QTRY_VERIFY(parent->control->active);
    QTRY_VERIFY(!transient->control->active);
    QTRY_VERIFY(!anotherClient->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{anotherClient, parent, transient}));

    // Go back to the initial setup.
    win::activate_window(*Test::app()->base.space, *anotherClient);
    QTRY_VERIFY(!parent->control->active);
    QTRY_VERIFY(!transient->control->active);
    QTRY_VERIFY(anotherClient->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient, anotherClient}));

    // If we activate the transient, the parent should be raised too.
    win::activate_window(*Test::app()->base.space, *transient);
    QTRY_VERIFY(!parent->control->active);
    QTRY_VERIFY(transient->control->active);
    QTRY_VERIFY(!anotherClient->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{anotherClient, parent, transient}));
}

void StackingOrderTest::testDeletedTransient()
{
    // This test verifies that deleted transients are kept above their
    // old parents.

    // Create the parent.
    auto parentSurface = Test::create_surface();
    QVERIFY(parentSurface);
    auto parentShellSurface = Test::create_xdg_shell_toplevel(parentSurface);
    QVERIFY(parentShellSurface);
    auto parent = Test::render_and_wait_for_shown(parentSurface, QSize(256, 256), Qt::blue);
    QVERIFY(parent);
    QVERIFY(parent->control->active);
    QVERIFY(!parent->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent}));

    // Create the first transient.
    auto transient1Surface = Test::create_surface();
    QVERIFY(transient1Surface);
    auto transient1ShellSurface = Test::create_xdg_shell_toplevel(transient1Surface);
    QVERIFY(transient1ShellSurface);
    transient1ShellSurface->setTransientFor(parentShellSurface.get());
    auto transient1 = Test::render_and_wait_for_shown(transient1Surface, QSize(128, 128), Qt::red);
    QVERIFY(transient1);
    QTRY_VERIFY(transient1->control->active);
    QVERIFY(transient1->transient->lead());
    QCOMPARE(transient1->transient->lead(), parent);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient1}));

    // Create the second transient.
    auto transient2Surface = Test::create_surface();
    QVERIFY(transient2Surface);

    auto transient2ShellSurface = Test::create_xdg_shell_toplevel(transient2Surface);
    QVERIFY(transient2ShellSurface);

    transient2ShellSurface->setTransientFor(transient1ShellSurface.get());

    auto transient2 = Test::render_and_wait_for_shown(transient2Surface, QSize(128, 128), Qt::red);
    QVERIFY(transient2);

    QTRY_VERIFY(transient2->control->active);
    QVERIFY(transient2->transient->lead());
    QCOMPARE(transient2->transient->lead(), transient1);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient1, transient2}));

    // Activate the parent, both transients have to be above it.
    win::activate_window(*Test::app()->base.space, *parent);
    QTRY_VERIFY(parent->control->active);
    QTRY_VERIFY(!transient1->control->active);
    QTRY_VERIFY(!transient2->control->active);

    // Close the top-most transient.
    connect(transient2->space.qobject.get(),
            &win::space::qobject_t::remnant_created,
            transient2->qobject.get(),
            [](auto win_id) {
                std::visit(overload{[&](auto&& win) { win->remnant->ref(); }},
                           Test::app()->base.space->windows_map.at(win_id));
            });

    QSignalSpy windowClosedSpy(transient2->space.qobject.get(),
                               &win::space::qobject_t::remnant_created);
    QVERIFY(windowClosedSpy.isValid());
    transient2ShellSurface.reset();
    transient2Surface.reset();
    QVERIFY(windowClosedSpy.wait());

    auto del_signal_id = windowClosedSpy.front().front().value<quint32>();
    auto deletedTransient = create_deleted<Test::wayland_window>(
        Test::app()->base.space->windows_map.at(del_signal_id));
    QVERIFY(deletedTransient);

    // The deleted transient still has to be above its old parent (transient1).
    QTRY_VERIFY(parent->control->active);
    QTRY_VERIFY(!transient1->control->active);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{parent, transient1, deletedTransient.get()}));
}

void StackingOrderTest::testGroupTransientIsAboveWindowGroup()
{
    // This test verifies that group transients are always above other
    // window group members.

    const QRect geometry = QRect(0, 0, 128, 128);

    // We need to wait until the remnant from previous test is gone.
    QTRY_VERIFY(Test::app()->base.space->windows.empty());

    auto conn = create_xcb_connection();

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());

    // Create the group leader.
    xcb_window_t leaderWid = createGroupWindow(conn.get(), geometry);
    xcb_map_window(conn.get(), leaderWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto leader = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(leader);
    QVERIFY(leader->control->active);
    QCOMPARE(leader->xcb_windows.client, leaderWid);
    QVERIFY(!leader->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader}));

    // Create another group member.
    windowCreatedSpy.clear();
    xcb_window_t member1Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member1Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto member1 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member1);
    QVERIFY(member1->control->active);
    QCOMPARE(member1->xcb_windows.client, member1Wid);
    QCOMPARE(member1->group, leader->group);
    QVERIFY(!member1->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1}));

    // Create yet another group member.
    windowCreatedSpy.clear();
    xcb_window_t member2Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member2Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto member2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member2);
    QVERIFY(member2->control->active);
    QCOMPARE(member2->xcb_windows.client, member2Wid);
    QCOMPARE(member2->group, leader->group);
    QVERIFY(!member2->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2}));

    // Create a group transient.
    windowCreatedSpy.clear();
    xcb_window_t transientWid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_icccm_set_wm_transient_for(conn.get(), transientWid, rootWindow());

    // Currently, we have some weird bug workaround: if a group transient
    // is a non-modal dialog, then it won't be kept above its window group.
    // We need to explicitly specify window type, otherwise the window type
    // will be deduced to _NET_WM_WINDOW_TYPE_DIALOG because we set transient
    // for before (the EWMH spec says to do that).
    xcb_atom_t net_wm_window_type
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE"), false, conn.get());
    xcb_atom_t net_wm_window_type_normal
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE_NORMAL"), false, conn.get());
    xcb_change_property(conn.get(),                // c
                        XCB_PROP_MODE_REPLACE,     // mode
                        transientWid,              // window
                        net_wm_window_type,        // property
                        XCB_ATOM_ATOM,             // type
                        32,                        // format
                        1,                         // data_len
                        &net_wm_window_type_normal // data
    );

    xcb_map_window(conn.get(), transientWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto transient = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(transient);
    QVERIFY(transient->control->active);
    QCOMPARE(transient->xcb_windows.client, transientWid);
    QCOMPARE(transient->group, leader->group);
    QVERIFY(transient->transient->lead());
    QVERIFY(transient->groupTransient());
    QVERIFY(!win::is_dialog(transient)); // See above why

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));

    // If we activate any member of the window group, the transient will be above it.
    win::activate_window(*Test::app()->base.space, *leader);
    QTRY_VERIFY(leader->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{member1, member2, leader, transient}));

    win::activate_window(*Test::app()->base.space, *member1);
    QTRY_VERIFY(member1->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{member2, leader, member1, transient}));

    win::activate_window(*Test::app()->base.space, *member2);
    QTRY_VERIFY(member2->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));

    win::activate_window(*Test::app()->base.space, *transient);
    QTRY_VERIFY(transient->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));
}

void StackingOrderTest::testRaiseGroupTransient()
{
    const QRect geometry = QRect(0, 0, 128, 128);

    auto conn = create_xcb_connection();

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());

    // Create the group leader.
    xcb_window_t leaderWid = createGroupWindow(conn.get(), geometry);
    xcb_map_window(conn.get(), leaderWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto leader = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(leader);
    QVERIFY(leader->control->active);
    QCOMPARE(leader->xcb_windows.client, leaderWid);
    QVERIFY(!leader->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader}));

    // Create another group member.
    windowCreatedSpy.clear();
    xcb_window_t member1Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member1Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto member1 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member1);
    QVERIFY(member1->control->active);
    QCOMPARE(member1->xcb_windows.client, member1Wid);
    QCOMPARE(member1->group, leader->group);
    QVERIFY(!member1->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1}));

    // Create yet another group member.
    windowCreatedSpy.clear();
    xcb_window_t member2Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member2Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto member2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member2);
    QVERIFY(member2->control->active);
    QCOMPARE(member2->xcb_windows.client, member2Wid);
    QCOMPARE(member2->group, leader->group);
    QVERIFY(!member2->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2}));

    // Create a group transient.
    windowCreatedSpy.clear();
    xcb_window_t transientWid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_icccm_set_wm_transient_for(conn.get(), transientWid, rootWindow());

    // Currently, we have some weird bug workaround: if a group transient
    // is a non-modal dialog, then it won't be kept above its window group.
    // We need to explicitly specify window type, otherwise the window type
    // will be deduced to _NET_WM_WINDOW_TYPE_DIALOG because we set transient
    // for before (the EWMH spec says to do that).
    xcb_atom_t net_wm_window_type
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE"), false, conn.get());
    xcb_atom_t net_wm_window_type_normal
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE_NORMAL"), false, conn.get());
    xcb_change_property(conn.get(),                // c
                        XCB_PROP_MODE_REPLACE,     // mode
                        transientWid,              // window
                        net_wm_window_type,        // property
                        XCB_ATOM_ATOM,             // type
                        32,                        // format
                        1,                         // data_len
                        &net_wm_window_type_normal // data
    );

    xcb_map_window(conn.get(), transientWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());

    auto transient = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(transient);
    QVERIFY(transient->control->active);
    QCOMPARE(transient->xcb_windows.client, transientWid);
    QCOMPARE(transient->group, leader->group);
    QVERIFY(transient->transient->lead());
    QVERIFY(transient->groupTransient());
    QVERIFY(!win::is_dialog(transient)); // See above why

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));

    // Create a Wayland client that is not a member of the window group.
    auto anotherSurface = Test::create_surface();
    QVERIFY(anotherSurface);
    auto anotherShellSurface = Test::create_xdg_shell_toplevel(anotherSurface);
    QVERIFY(anotherShellSurface);
    auto anotherClient
        = Test::render_and_wait_for_shown(anotherSurface, QSize(128, 128), Qt::green);
    QVERIFY(anotherClient);
    QVERIFY(anotherClient->control->active);
    QVERIFY(!anotherClient->transient->lead());

    QCOMPARE(
        Test::app()->base.space->stacking.order.stack,
        (std::deque<Test::space::window_t>{leader, member1, member2, transient, anotherClient}));

    // If we activate the leader, then only it and the transient have to be raised.
    win::activate_window(*Test::app()->base.space, *leader);
    QTRY_VERIFY(leader->control->active);
    QCOMPARE(
        Test::app()->base.space->stacking.order.stack,
        (std::deque<Test::space::window_t>{member1, member2, anotherClient, leader, transient}));

    // If another member of the window group is activated, then the transient will
    // be above that member and the leader.
    win::activate_window(*Test::app()->base.space, *member2);
    QTRY_VERIFY(member2->control->active);
    QCOMPARE(
        Test::app()->base.space->stacking.order.stack,
        (std::deque<Test::space::window_t>{member1, anotherClient, leader, member2, transient}));

    // FIXME: If we activate the transient, only it will be raised.
    win::activate_window(*Test::app()->base.space, *anotherClient);
    QTRY_VERIFY(anotherClient->control->active);
    QCOMPARE(
        Test::app()->base.space->stacking.order.stack,
        (std::deque<Test::space::window_t>{member1, leader, member2, transient, anotherClient}));

    win::activate_window(*Test::app()->base.space, *transient);
    QTRY_VERIFY(transient->control->active);
    QCOMPARE(
        Test::app()->base.space->stacking.order.stack,
        (std::deque<Test::space::window_t>{anotherClient, member1, leader, member2, transient}));
}

void StackingOrderTest::testDeletedGroupTransient()
{
    // This test verifies that deleted group transients are kept above their
    // old window groups.

    const QRect geometry = QRect(0, 0, 128, 128);

    auto conn = create_xcb_connection();

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());

    // Create the group leader.
    xcb_window_t leaderWid = createGroupWindow(conn.get(), geometry);
    xcb_map_window(conn.get(), leaderWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto leader = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(leader);
    QVERIFY(leader->control->active);
    QCOMPARE(leader->xcb_windows.client, leaderWid);
    QVERIFY(!leader->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader}));

    // Create another group member.
    windowCreatedSpy.clear();
    xcb_window_t member1Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member1Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto member1 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member1);
    QVERIFY(member1->control->active);
    QCOMPARE(member1->xcb_windows.client, member1Wid);
    QCOMPARE(member1->group, leader->group);
    QVERIFY(!member1->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1}));

    // Create yet another group member.
    windowCreatedSpy.clear();
    xcb_window_t member2Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member2Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto member2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member2);
    QVERIFY(member2->control->active);
    QCOMPARE(member2->xcb_windows.client, member2Wid);
    QCOMPARE(member2->group, leader->group);
    QVERIFY(!member2->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2}));

    // Create a group transient.
    windowCreatedSpy.clear();
    xcb_window_t transientWid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_icccm_set_wm_transient_for(conn.get(), transientWid, rootWindow());

    // Currently, we have some weird bug workaround: if a group transient
    // is a non-modal dialog, then it won't be kept above its window group.
    // We need to explicitly specify window type, otherwise the window type
    // will be deduced to _NET_WM_WINDOW_TYPE_DIALOG because we set transient
    // for before (the EWMH spec says to do that).
    xcb_atom_t net_wm_window_type
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE"), false, conn.get());
    xcb_atom_t net_wm_window_type_normal
        = base::x11::xcb::atom(QByteArrayLiteral("_NET_WM_WINDOW_TYPE_NORMAL"), false, conn.get());
    xcb_change_property(conn.get(),                // c
                        XCB_PROP_MODE_REPLACE,     // mode
                        transientWid,              // window
                        net_wm_window_type,        // property
                        XCB_ATOM_ATOM,             // type
                        32,                        // format
                        1,                         // data_len
                        &net_wm_window_type_normal // data
    );

    xcb_map_window(conn.get(), transientWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto transient = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(transient);
    QVERIFY(transient->control->active);
    QCOMPARE(transient->xcb_windows.client, transientWid);
    QCOMPARE(transient->group, leader->group);
    QVERIFY(transient->transient->lead());
    QVERIFY(transient->groupTransient());
    QVERIFY(!win::is_dialog(transient)); // See above why

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));

    if (!transient->render_data.ready_for_painting) {
        QSignalSpy window_shown_spy(transient->qobject.get(), &win::window_qobject::windowShown);
        QVERIFY(window_shown_spy.isValid());
        QVERIFY(window_shown_spy.wait());
    }

    // Unmap the transient.
    connect(transient->space.qobject.get(),
            &win::space::qobject_t::remnant_created,
            transient->qobject.get(),
            [](auto win_id) {
                std::visit(overload{[&](auto&& win) { win->remnant->ref(); }},
                           Test::app()->base.space->windows_map.at(win_id));
            });

    QSignalSpy windowClosedSpy(transient->space.qobject.get(),
                               &win::space::qobject_t::remnant_created);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(conn.get(), transientWid);
    xcb_flush(conn.get());
    QVERIFY(windowClosedSpy.wait());

    auto del_signal_id = windowClosedSpy.front().front().value<quint32>();
    auto deletedTransient = create_deleted<Test::space::x11_window>(
        Test::app()->base.space->windows_map.at(del_signal_id));
    QVERIFY(deletedTransient.get());

    // The transient has to be above each member of the window group.
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, deletedTransient.get()}));
}

void StackingOrderTest::testDontKeepAboveNonModalDialogGroupTransients()
{
    // Bug 76026

    const QRect geometry = QRect(0, 0, 128, 128);

    auto conn = create_xcb_connection();

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());

    // Create the group leader.
    xcb_window_t leaderWid = createGroupWindow(conn.get(), geometry);
    xcb_map_window(conn.get(), leaderWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto leader = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(leader);
    QVERIFY(leader->control->active);
    QCOMPARE(leader->xcb_windows.client, leaderWid);
    QVERIFY(!leader->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader}));

    // Create another group member.
    windowCreatedSpy.clear();
    xcb_window_t member1Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member1Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto member1 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member1);
    QVERIFY(member1->control->active);
    QCOMPARE(member1->xcb_windows.client, member1Wid);
    QCOMPARE(member1->group, leader->group);
    QVERIFY(!member1->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1}));

    // Create yet another group member.
    windowCreatedSpy.clear();
    xcb_window_t member2Wid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_map_window(conn.get(), member2Wid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto member2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(member2);
    QVERIFY(member2->control->active);
    QCOMPARE(member2->xcb_windows.client, member2Wid);
    QCOMPARE(member2->group, leader->group);
    QVERIFY(!member2->transient->lead());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2}));

    // Create a group transient.
    windowCreatedSpy.clear();
    xcb_window_t transientWid = createGroupWindow(conn.get(), geometry, leaderWid);
    xcb_icccm_set_wm_transient_for(conn.get(), transientWid, rootWindow());
    xcb_map_window(conn.get(), transientWid);
    xcb_flush(conn.get());

    QVERIFY(windowCreatedSpy.wait());
    auto transient = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(transient);
    QVERIFY(transient->control->active);
    QCOMPARE(transient->xcb_windows.client, transientWid);
    QCOMPARE(transient->group, leader->group);
    QVERIFY(transient->transient->lead());
    QVERIFY(transient->groupTransient());
    QVERIFY(win::is_dialog(transient));
    QVERIFY(!transient->transient->modal());

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));

    win::activate_window(*Test::app()->base.space, *leader);
    QTRY_VERIFY(leader->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{member1, member2, transient, leader}));

    win::activate_window(*Test::app()->base.space, *member1);
    QTRY_VERIFY(member1->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{member2, transient, leader, member1}));

    win::activate_window(*Test::app()->base.space, *member2);
    QTRY_VERIFY(member2->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{transient, leader, member1, member2}));

    win::activate_window(*Test::app()->base.space, *transient);
    QTRY_VERIFY(transient->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{leader, member1, member2, transient}));
}

void StackingOrderTest::testKeepAbove()
{
    // This test verifies that "keep-above" windows are kept above other windows.

    // Create the first client.
    auto clientASurface = Test::create_surface();
    QVERIFY(clientASurface);
    auto clientAShellSurface = Test::create_xdg_shell_toplevel(clientASurface);
    QVERIFY(clientAShellSurface);
    auto clientA = Test::render_and_wait_for_shown(clientASurface, QSize(128, 128), Qt::green);
    QVERIFY(clientA);
    QVERIFY(clientA->control->active);
    QVERIFY(!clientA->control->keep_above);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientA}));

    // Create the second client.
    auto clientBSurface = Test::create_surface();
    QVERIFY(clientBSurface);
    auto clientBShellSurface = Test::create_xdg_shell_toplevel(clientBSurface);
    QVERIFY(clientBShellSurface);
    auto clientB = Test::render_and_wait_for_shown(clientBSurface, QSize(128, 128), Qt::green);
    QVERIFY(clientB);
    QVERIFY(clientB->control->active);
    QVERIFY(!clientB->control->keep_above);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientA, clientB}));

    // Go to the initial test position.
    win::activate_window(*Test::app()->base.space, *clientA);
    QTRY_VERIFY(clientA->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientB, clientA}));

    // Set the "keep-above" flag on the client B, it should go above other clients.
    {
        blocker block(Test::app()->base.space->stacking.order);
        win::set_keep_above(clientB, true);
    }

    QVERIFY(clientB->control->keep_above);
    QVERIFY(!clientB->control->active);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientA, clientB}));
}

void StackingOrderTest::testKeepBelow()
{
    // This test verifies that "keep-below" windows are kept below other windows.

    // Create the first client.
    auto clientASurface = Test::create_surface();
    QVERIFY(clientASurface);
    auto clientAShellSurface = Test::create_xdg_shell_toplevel(clientASurface);
    QVERIFY(clientAShellSurface);
    auto clientA = Test::render_and_wait_for_shown(clientASurface, QSize(128, 128), Qt::green);
    QVERIFY(clientA);
    QVERIFY(clientA->control->active);
    QVERIFY(!clientA->control->keep_below);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientA}));

    // Create the second client.
    auto clientBSurface = Test::create_surface();
    QVERIFY(clientBSurface);
    auto clientBShellSurface = Test::create_xdg_shell_toplevel(clientBSurface);
    QVERIFY(clientBShellSurface);
    auto clientB = Test::render_and_wait_for_shown(clientBSurface, QSize(128, 128), Qt::green);
    QVERIFY(clientB);
    QVERIFY(clientB->control->active);
    QVERIFY(!clientB->control->keep_below);

    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientA, clientB}));

    // Set the "keep-below" flag on the client B, it should go below other clients.
    {
        blocker block(Test::app()->base.space->stacking.order);
        win::set_keep_below(clientB, true);
    }

    QVERIFY(clientB->control->active);
    QVERIFY(clientB->control->keep_below);
    QCOMPARE(Test::app()->base.space->stacking.order.stack,
             (std::deque<Test::space::window_t>{clientB, clientA}));
}

}

WAYLANDTEST_MAIN(KWin::StackingOrderTest)
#include "stacking_order_test.moc"
