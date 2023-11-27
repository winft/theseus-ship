/*
    SPDX-FileCopyrightText: 2012, 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

struct subspace_north {
    subspace_north(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_north_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

struct subspace_south {
    subspace_south(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_south_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

struct subspace_west {
    subspace_west(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_west_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

struct subspace_east {
    subspace_east(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_east_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

struct subspace_successor {
    subspace_successor(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_successor_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

struct subspace_predecessor {
    subspace_predecessor(win::wayland::subspace_manager& manager)
        : manager{manager}
    {
    }
    win::subspace& operator()(win::subspace& subspace, bool wrap)
    {
        return win::subspaces_get_predecessor_of(manager, subspace, wrap);
    }

    win::wayland::subspace_manager& manager;
};

template<typename Functor, typename Data>
void test_direction(test::setup& setup, Data const& test_data, std::string const& action_name)
{
    auto& vd_manager = setup.base->mod.space->subspace_manager;

    win::subspace_manager_set_count(*vd_manager, test_data.init_count);
    win::subspace_manager_set_rows(*vd_manager, 2);
    win::subspaces_set_current(*vd_manager, test_data.init_current);

    Functor functor(*vd_manager);
    QCOMPARE(functor(*vd_manager->current, test_data.wrap).x11DesktopNumber(), test_data.result);

    win::subspace_manager_set_nav_wraps(*vd_manager, test_data.wrap);

    auto action = vd_manager->qobject->findChild<QAction*>(QString::fromStdString(action_name));
    QVERIFY(action);
    action->trigger();

    QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), test_data.result);

    auto init_subspace = win::subspaces_get_for_x11id(*vd_manager, test_data.init_current);
    QVERIFY(init_subspace);

    auto& result = functor(*init_subspace, test_data.wrap);
    QCOMPARE(result.x11DesktopNumber(), test_data.result);
}

TEST_CASE("subspace", "[win]")
{
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("subspace", operation_mode);
    setup.start();

// Deactivated for now. We don't set the properties anymore via Xwayland since it's assumed there
// are no valid use cases.
#if 0
    if (setup.base->x11_data.connection) {
        // verify the current desktop x11 property on startup, see BUG: 391034
        base::x11::xcb::atom currentDesktopAtom("_NET_CURRENT_DESKTOP",
                                                setup.base->x11_data.connection);
        QVERIFY(currentDesktopAtom.is_valid());
        base::x11::xcb::property currentDesktop(setup.base->x11_data.connection,
                                                0,
                                                setup.base->x11_data.root_window,
                                                currentDesktopAtom,
                                                XCB_ATOM_CARDINAL,
                                                0,
                                                1);
        bool ok = true;
        QCOMPARE(currentDesktop.value(0, &ok), 0);
        QVERIFY(ok);
    }
#endif

    setup_wayland_connection();
    auto& vd_manager = setup.base->mod.space->subspace_manager;
    win::subspace_manager_set_count(*vd_manager, 1);
    win::subspaces_set_current(*vd_manager, 0u);

    SECTION("count")
    {
        struct data {
            unsigned int request;
            unsigned int result;
            bool signal;
            bool removed_signal;
        };

        auto test_data = GENERATE(
            // Minimum
            data{1, 1, true, true},
            // Below minimum
            data{0, 1, true, true},
            // Normal value
            data{10, 10, true, false},
            // Maximum
            data{win::wayland::subspace_manager::max_count,
                 win::wayland::subspace_manager::max_count,
                 true,
                 false},
            // Above maximum
            data{win::wayland::subspace_manager::max_count + 1,
                 win::wayland::subspace_manager::max_count,
                 true,
                 false},
            // Unchanged
            data{2, 2, false, false});

        QCOMPARE(vd_manager->subspaces.size(), 1);

        // start with a useful subspace count
        auto const count_init_value = 2;
        win::subspace_manager_set_count(*vd_manager, count_init_value);

        QSignalSpy spy(vd_manager->qobject.get(), &win::subspace_manager_qobject::countChanged);
        QSignalSpy subspacesRemoved(vd_manager->qobject.get(),
                                    &win::subspace_manager_qobject::subspace_removed);

        auto vdToRemove = vd_manager->subspaces.back();

        win::subspace_manager_set_count(*vd_manager, test_data.request);
        QCOMPARE(vd_manager->subspaces.size(), test_data.result);
        QCOMPARE(spy.isEmpty(), !test_data.signal);

        if (!spy.isEmpty()) {
            auto arguments = spy.takeFirst();
            QCOMPARE(arguments.count(), 2);
            QCOMPARE(arguments.at(0).type(), QVariant::UInt);
            QCOMPARE(arguments.at(1).type(), QVariant::UInt);
            QCOMPARE(arguments.at(0).toUInt(), count_init_value);
            QCOMPARE(arguments.at(1).toUInt(), test_data.result);
        }

        QCOMPARE(subspacesRemoved.isEmpty(), !test_data.removed_signal);
        if (!subspacesRemoved.isEmpty()) {
            auto arguments = subspacesRemoved.takeFirst();
            QCOMPARE(arguments.count(), 1);
            QCOMPARE(arguments.at(0).value<win::subspace*>(), vdToRemove);
        }
    }

    SECTION("navigation wraps around")
    {
        struct data {
            bool init;
            bool request;
            bool result;
            bool signal;
        };

        auto test_data = GENERATE(
            // enable
            data{false, true, true, true},
            // disable
            data{true, false, false, true},
            // keep enabled
            data{true, true, true, false},
            // keep disabled
            data{false, false, false, false});

        QCOMPARE(vd_manager->nav_wraps, true);

        // set to init value
        win::subspace_manager_set_nav_wraps(*vd_manager, test_data.init);
        QCOMPARE(vd_manager->nav_wraps, test_data.init);

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::subspace_manager_qobject::nav_wraps_changed);
        win::subspace_manager_set_nav_wraps(*vd_manager, test_data.request);
        QCOMPARE(vd_manager->nav_wraps, test_data.result);
        QCOMPARE(spy.isEmpty(), !test_data.signal);
    }

    SECTION("current")
    {
        struct data {
            unsigned int count;
            unsigned int init;
            unsigned int request;
            unsigned int result;
            bool signal;
        };

        auto test_data = GENERATE(
            // lower
            data{4, 3, 2, 2, true},
            // higher
            data{4, 1, 2, 2, true},
            // maximum
            data{4, 1, 4, 4, true},
            // above maximum
            data{4, 1, 5, 1, false},
            // minimum
            data{4, 2, 1, 1, true},
            // below minimum
            data{4, 2, 0, 2, false},
            // unchanged
            data{4, 2, 2, 2, false});

        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), 1);

        win::subspace_manager_set_count(*vd_manager, test_data.count);
        REQUIRE(win::subspaces_set_current(*vd_manager, test_data.init) == (test_data.init != 1));
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), test_data.init);

        QSignalSpy spy(vd_manager->qobject.get(), &win::subspace_manager_qobject::current_changed);

        QCOMPARE(win::subspaces_set_current(*vd_manager, test_data.request), test_data.signal);
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), test_data.result);
        QCOMPARE(spy.isEmpty(), !test_data.signal);

        if (!spy.isEmpty()) {
            QList<QVariant> arguments = spy.takeFirst();
            QCOMPARE(arguments.count(), 2);
            QVERIFY(arguments.at(0).canConvert<win::subspace*>());
            QVERIFY(arguments.at(1).canConvert<win::subspace*>());
            QCOMPARE(arguments.at(0).value<win::subspace*>(),
                     win::subspaces_get_for_x11id(*vd_manager, test_data.init));
            QCOMPARE(arguments.at(1).value<win::subspace*>(),
                     win::subspaces_get_for_x11id(*vd_manager, test_data.result));
        }
    }

    SECTION("current change on count change")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            unsigned int request;
            unsigned int current;
            bool signal;
        };

        auto test_data = GENERATE(
            // increment
            data{4, 2, 5, 2, false},
            // increement on last
            data{4, 4, 5, 4, false},
            // decrement
            data{4, 2, 3, 2, false},
            // decrement on second last
            data{4, 3, 3, 3, false},
            // decrement on last
            data{4, 4, 3, 3, true},
            // multiple decrement
            data{4, 2, 1, 1, true});

        win::subspace_manager_set_count(*vd_manager, test_data.init_count);
        win::subspaces_set_current(*vd_manager, test_data.init_current);

        QSignalSpy spy(vd_manager->qobject.get(), &win::subspace_manager_qobject::current_changed);

        win::subspace_manager_set_count(*vd_manager, test_data.request);
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), test_data.current);
        QCOMPARE(spy.isEmpty(), !test_data.signal);
    }

    SECTION("next")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap
            data{4, 1, true, 2},
            // subspaces, no wrap
            data{4, 1, false, 2},
            // subspaces at end, wrap
            data{4, 4, true, 1},
            // subspaces at end, no wrap
            data{4, 4, false, 4});

        test_direction<subspace_successor>(setup, test_data, "Switch to Next Desktop");
    }

    SECTION("previous")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap
            data{4, 3, true, 2},
            // subspaces, no wrap
            data{4, 3, false, 2},
            // subspaces at start, wrap
            data{4, 1, true, 4},
            // subspaces at start, no wrap
            data{4, 1, false, 1});

        test_direction<subspace_predecessor>(setup, test_data, "Switch to Previous Desktop");
    }

    SECTION("left")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap, 1st row
            data{4, 2, true, 1},
            // subspaces, no wrap, 1st row
            data{4, 2, false, 1},
            // subspaces, wrap, 2nd row
            data{4, 4, true, 3},
            // subspaces, no wrap, 2nd row
            data{4, 4, false, 3},
            // subspaces at start, wrap, 1st row
            data{4, 1, true, 2},
            // subspaces at start, no wrap, 1st row
            data{4, 1, false, 1},
            // subspaces at start, wrap, 2nd row
            data{4, 3, true, 4},
            // subspaces at start, no wrap, 2nd row
            data{4, 3, false, 3},
            // non symmetric, start
            data{5, 5, false, 4},
            // non symmetric, end, no wrap
            data{5, 4, false, 4},
            // non symmetric, end, wrap
            data{5, 4, true, 5});

        test_direction<subspace_west>(setup, test_data, "Switch One Desktop to the Left");
    }

    SECTION("right")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap, 1st row
            data{4, 1, true, 2},
            // subspaces, no wrap, 1st row
            data{4, 1, false, 2},
            // subspaces, wrap, 2nd row
            data{4, 3, true, 4},
            // subspaces, no wrap, 2nd row
            data{4, 3, false, 4},
            // subspaces at start, wrap, 1st row
            data{4, 2, true, 1},
            // subspaces at start, no wrap, 1st row
            data{4, 2, false, 2},
            // subspaces at start, wrap, 2nd row
            data{4, 4, true, 3},
            // subspaces at start, no wrap, 2nd row
            data{4, 4, false, 4},
            // non symmetric, start
            data{5, 4, true, 5},
            // non symmetric, end, no wrap
            data{5, 5, false, 5},
            // non symmetric, end, wrap
            data{5, 5, true, 4});

        test_direction<subspace_east>(setup, test_data, "Switch One Desktop to the Right");
    }

    SECTION("above")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap, 1st column
            data{4, 3, true, 1},
            // subspaces, no wrap, 1st column
            data{4, 3, false, 1},
            // subspaces, wrap, 2nd column
            data{4, 4, true, 2},
            // subspaces, no wrap, 2nd column
            data{4, 4, false, 2},
            // subspaces at start, wrap, 1st column
            data{4, 1, true, 3},
            // subspaces at start, no wrap, 1st column
            data{4, 1, false, 1},
            // subspaces at start, wrap, 2nd column
            data{4, 2, true, 4},
            // subspaces at start, no wrap, 2nd column
            data{4, 2, false, 2});

        test_direction<subspace_north>(setup, test_data, "Switch One Desktop Up");
    }

    SECTION("below")
    {
        struct data {
            unsigned int init_count;
            unsigned int init_current;
            bool wrap;
            unsigned int result;
        };

        auto test_data = GENERATE(
            // one subspace, wrap
            data{1, 1, true, 1},
            // one subspace, no wrap
            data{1, 1, false, 1},
            // subspaces, wrap, 1st column
            data{4, 1, true, 3},
            // subspaces, no wrap, 1st column
            data{4, 1, false, 3},
            // subspaces, wrap, 2nd column
            data{4, 2, true, 4},
            // subspaces, no wrap, 2nd column
            data{4, 2, false, 4},
            // subspaces at start, wrap, 1st column
            data{4, 3, true, 1},
            // subspaces at start, no wrap, 1st column
            data{4, 3, false, 3},
            // subspaces at start, wrap, 2nd column
            data{4, 4, true, 2},
            // subspaces at start, no wrap, 2nd column
            data{4, 4, false, 4});

        test_direction<subspace_south>(setup, test_data, "Switch One Desktop Down");
    }

    SECTION("update grid")
    {
        struct data {
            unsigned int init_count;
            QSize size;
            Qt::Orientation orientation;
            QPoint coords;
            unsigned int subspace;
        };

        auto test_data = GENERATE(data{1, {1, 1}, Qt::Horizontal, {0, 0}, 1},
                                  data{1, {1, 1}, Qt::Horizontal, {0, 0}, 1},
                                  data{1, {1, 1}, Qt::Vertical, {1, 0}, 0},
                                  data{1, {1, 1}, Qt::Vertical, {0, 1}, 0},
                                  data{2, {2, 1}, Qt::Horizontal, {0, 0}, 1},
                                  data{2, {2, 1}, Qt::Horizontal, {1, 0}, 2},
                                  data{2, {2, 1}, Qt::Horizontal, {0, 1}, 0},
                                  data{2, {2, 1}, Qt::Horizontal, {2, 0}, 0},
                                  data{2, {2, 1}, Qt::Vertical, {0, 0}, 1},
                                  data{2, {2, 1}, Qt::Vertical, {1, 0}, 2},
                                  data{2, {2, 1}, Qt::Vertical, {0, 1}, 0},
                                  data{2, {2, 1}, Qt::Vertical, {2, 0}, 0},
                                  data{3, {2, 2}, Qt::Horizontal, {0, 0}, 1},
                                  data{3, {2, 2}, Qt::Horizontal, {1, 0}, 2},
                                  data{3, {2, 2}, Qt::Horizontal, {0, 1}, 3},
                                  data{3, {2, 2}, Qt::Horizontal, {1, 1}, 0},
                                  data{4, {4, 1}, Qt::Horizontal, {0, 0}, 1},
                                  data{4, {4, 1}, Qt::Horizontal, {1, 0}, 2},
                                  data{4, {4, 1}, Qt::Horizontal, {2, 0}, 3},
                                  data{4, {4, 1}, Qt::Horizontal, {3, 0}, 4},
                                  data{4, {1, 4}, Qt::Vertical, {0, 0}, 1},
                                  data{4, {1, 4}, Qt::Vertical, {0, 1}, 2},
                                  data{4, {1, 4}, Qt::Vertical, {0, 2}, 3},
                                  data{4, {1, 4}, Qt::Vertical, {0, 3}, 4},
                                  data{4, {2, 2}, Qt::Horizontal, {0, 0}, 1},
                                  data{4, {2, 2}, Qt::Horizontal, {1, 0}, 2},
                                  data{4, {2, 2}, Qt::Horizontal, {0, 1}, 3},
                                  data{4, {2, 2}, Qt::Horizontal, {1, 1}, 4},
                                  data{4, {2, 2}, Qt::Horizontal, {0, 3}, 0});

        win::subspace_manager_set_count(*vd_manager, test_data.init_count);

        win::subspace_grid grid;

        QCOMPARE(vd_manager->subspaces.size(), int(test_data.init_count));

        grid.update(test_data.size, test_data.orientation, vd_manager->subspaces);
        QCOMPARE(grid.size(), test_data.size);
        QCOMPARE(grid.width(), test_data.size.width());
        QCOMPARE(grid.height(), test_data.size.height());

        QCOMPARE(grid.at(test_data.coords),
                 win::subspaces_get_for_x11id(*vd_manager, test_data.subspace));

        if (test_data.subspace != 0) {
            QCOMPARE(grid.gridCoords(win::subspaces_get_for_x11id(*vd_manager, test_data.subspace)),
                     test_data.coords);
        }
    }

    SECTION("update layout")
    {
        // call update layout - implicitly through setCount

        struct data {
            unsigned int subspace;
            QSize result;
        };

        auto test_data = GENERATE(data{1, {1, 1}},
                                  data{2, {1, 2}},
                                  data{3, {2, 2}},
                                  data{4, {2, 2}},
                                  data{5, {3, 2}},
                                  data{6, {3, 2}},
                                  data{7, {4, 2}},
                                  data{8, {4, 2}},
                                  data{9, {5, 2}},
                                  data{10, {5, 2}},
                                  data{11, {6, 2}},
                                  data{12, {6, 2}},
                                  data{13, {7, 2}},
                                  data{14, {7, 2}},
                                  data{15, {8, 2}},
                                  data{16, {8, 2}},
                                  data{17, {9, 2}},
                                  data{18, {9, 2}},
                                  data{19, {10, 2}},
                                  data{20, {10, 2}});

        QSignalSpy spy(vd_manager->qobject.get(), &win::subspace_manager_qobject::layoutChanged);
        QVERIFY(spy.isValid());

        if (test_data.subspace == 1) {
            // Must be changed back and forth from our default so the spy fires.
            win::subspace_manager_set_count(*vd_manager, 2);
        }

        win::subspace_manager_set_count(*vd_manager, test_data.subspace);
        win::subspace_manager_set_rows(*vd_manager, 2);

        QCOMPARE(vd_manager->grid.size(), test_data.result);
        QVERIFY(!spy.empty());

        auto const& arguments = spy.back();
        QCOMPARE(arguments.at(0).toInt(), test_data.result.width());
        QCOMPARE(arguments.at(1).toInt(), test_data.result.height());

        spy.clear();

        // calling update layout again should not change anything
        win::subspace_manager_update_layout(*vd_manager);
        QCOMPARE(vd_manager->grid.size(), test_data.result);
        QCOMPARE(spy.count(), 1);

        auto const& arguments2 = spy.back();
        QCOMPARE(arguments2.at(0).toInt(), test_data.result.width());
        QCOMPARE(arguments2.at(1).toInt(), test_data.result.height());
    }

    SECTION("name")
    {
        struct data {
            unsigned int init_count;
            unsigned int subspace;
            std::string subspace_name;
        };

        auto test_data = GENERATE(data{4, 1, "Desktop 1"},
                                  data{4, 2, "Desktop 2"},
                                  data{4, 3, "Desktop 3"},
                                  data{4, 4, "Desktop 4"},
                                  data{5, 5, "Desktop 5"});

        win::subspace_manager_set_count(*vd_manager, test_data.init_count);
        REQUIRE(win::subspace_manager_get_subspace_name(*vd_manager, test_data.subspace)
                == QString::fromStdString(test_data.subspace_name));
    }

    SECTION("switch to shortcut")
    {
        win::subspace_manager_set_count(*vd_manager, vd_manager->max_count);
        win::subspaces_set_current(*vd_manager, vd_manager->max_count);

        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), vd_manager->max_count);
        //    vd_manager->initShortcuts();
        auto const toDesktop = QStringLiteral("Switch to Desktop %1");

        for (uint i = 1; i <= vd_manager->max_count; ++i) {
            const QString desktop(toDesktop.arg(i));
            QAction* action = vd_manager->qobject->findChild<QAction*>(desktop);
            QVERIFY2(action, desktop.toUtf8().constData());
            action->trigger();
            QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), i);
        }

        // should still be on max
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), vd_manager->max_count);
    }

    SECTION("change rows")
    {
        win::subspace_manager_set_count(*vd_manager, 4);
        win::subspace_manager_set_rows(*vd_manager, 4);
        QCOMPARE(vd_manager->rows, 4);

        win::subspace_manager_set_rows(*vd_manager, 5);
        QCOMPARE(vd_manager->rows, 4);

        win::subspace_manager_set_count(*vd_manager, 2);
        REQUIRE(vd_manager->rows == 2);
    }

    SECTION("load")
    {
        // No config yet, load should not change anything.
        win::subspace_manager_load(*vd_manager);
        QCOMPARE(vd_manager->subspaces.size(), 1);

        // Empty config should create one subspace.
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        vd_manager->config = config;
        win::subspace_manager_load(*vd_manager);
        QCOMPARE(vd_manager->subspaces.size(), 1);

        // Setting a sensible number.
        config->group(QStringLiteral("Desktops")).writeEntry("Number", 4);
        win::subspace_manager_load(*vd_manager);
        QCOMPARE(vd_manager->subspaces.size(), 4);

        // Setting the config value and reloading should update.
        config->group(QStringLiteral("Desktops")).writeEntry("Number", 5);
        win::subspace_manager_load(*vd_manager);
        QCOMPARE(vd_manager->subspaces.size(), 5);
    }

    SECTION("save")
    {
        win::subspace_manager_set_count(*vd_manager, 4);

        // No config yet, just to ensure it actually works.
        win::subspace_manager_save(*vd_manager);

        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        vd_manager->config = config;

        REQUIRE(config->hasGroup(QStringLiteral("Desktops")));

        // Now save should create the group "Desktops".
        win::subspace_manager_save(*vd_manager);
        QCOMPARE(config->hasGroup(QStringLiteral("Desktops")), true);

        auto subspaces = config->group(QStringLiteral("Desktops"));
        QCOMPARE(subspaces.readEntry<int>("Number", 1), 4);
        QCOMPARE(subspaces.hasKey("Name_1"), false);
        QCOMPARE(subspaces.hasKey("Name_2"), false);
        QCOMPARE(subspaces.hasKey("Name_3"), false);
        QCOMPARE(subspaces.hasKey("Name_4"), false);
    }

// Deactivated for now. We don't set the properties anymore via Xwayland since it's assumed there
// are no valid use cases.
#if 0
    SECTION("net current desktop")
    {
        if (!setup.base->x11_data.connection) {
            QSKIP("Skipped on Wayland only");
        }

        QCOMPARE(vd_manager->subspaces.size(), 1u);
        win::subspace_manager_set_count(*vd_manager, 4);
        QCOMPARE(vd_manager->subspaces.size(), 4u);

        base::x11::xcb::atom currentDesktopAtom("_NET_CURRENT_DESKTOP",
                                                setup.base->x11_data.connection);
        QVERIFY(currentDesktopAtom.is_valid());
        base::x11::xcb::property currentDesktop(setup.base->x11_data.connection,
                                                0,
                                                setup.base->x11_data.root_window,
                                                currentDesktopAtom,
                                                XCB_ATOM_CARDINAL,
                                                0,
                                                1);
        bool ok = true;
        QCOMPARE(currentDesktop.value(0, &ok), 0);
        QVERIFY(ok);

        // go to subspace 2
        win::subspaces_set_current(*vd_manager, 2);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 1);
        QVERIFY(ok);

        // go to subspace 3
        win::subspaces_set_current(*vd_manager, 3);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 2);
        QVERIFY(ok);

        // go to subspace 4
        win::subspaces_set_current(*vd_manager, 4);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 3);
        QVERIFY(ok);

        // and back to first
        win::subspaces_set_current(*vd_manager, 1);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 0);
        QVERIFY(ok);
    }
#endif

    SECTION("last subspace removed")
    {
        // first create a new subspace
        QCOMPARE(vd_manager->subspaces.size(), 1u);
        win::subspace_manager_set_count(*vd_manager, 2);
        QCOMPARE(vd_manager->subspaces.size(), 2u);

        // switch to last subspace
        win::subspaces_set_current(*vd_manager, *vd_manager->subspaces.back());
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), 2u);

        // now create a window on this subspace
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_subspace(*client), 2);
        QCOMPARE(client->topo.subspaces.size(), 1u);
        QCOMPARE(vd_manager->current, client->topo.subspaces.front());

        // and remove last subspace
        win::subspace_manager_set_count(*vd_manager, 1);
        QCOMPARE(vd_manager->subspaces.size(), 1u);

        // now the client should be moved as well
        QCOMPARE(win::get_subspace(*client), 1);

        QCOMPARE(client->topo.subspaces.size(), 1u);
        QCOMPARE(vd_manager->current, client->topo.subspaces.front());
    }

    SECTION("window on multiple subspaces")
    {
        // first create two new subspaces
        QCOMPARE(vd_manager->subspaces.size(), 1u);
        win::subspace_manager_set_count(*vd_manager, 3);
        QCOMPARE(vd_manager->subspaces.size(), 3u);

        // switch to last subspace
        win::subspaces_set_current(*vd_manager, *vd_manager->subspaces.back());
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), 3u);

        // now create a window on this subspace
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_subspace(*client), 3u);
        QCOMPARE(client->topo.subspaces.size(), 1u);
        QCOMPARE(vd_manager->current, client->topo.subspaces.front());

        // Set the window on subspace 2 as well
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 2));
        QCOMPARE(client->topo.subspaces.size(), 2u);
        QCOMPARE(vd_manager->subspaces.at(2), client->topo.subspaces.at(0));
        QCOMPARE(vd_manager->subspaces.at(1), client->topo.subspaces.at(1));
        QVERIFY(win::on_subspace(*client, 2));
        QVERIFY(win::on_subspace(*client, 3));

        // leave subspace 3
        win::leave_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 3));
        QCOMPARE(client->topo.subspaces.size(), 1u);

        // leave subspace 2
        win::leave_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 2));
        QCOMPARE(client->topo.subspaces.size(), 0u);

        // we should be on all subspaces now
        QVERIFY(win::on_all_subspaces(*client));

        // put on subspace 1
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 1));
        QVERIFY(win::on_subspace(*client, 1));
        QVERIFY(!win::on_subspace(*client, 2));
        QVERIFY(!win::on_subspace(*client, 3));
        QCOMPARE(client->topo.subspaces.size(), 1u);

        // put on subspace 2
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 2));
        QVERIFY(win::on_subspace(*client, 1));
        QVERIFY(win::on_subspace(*client, 2));
        QVERIFY(!win::on_subspace(*client, 3));
        QCOMPARE(client->topo.subspaces.size(), 2u);

        // put on subspace 3
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 3));
        QVERIFY(win::on_subspace(*client, 1));
        QVERIFY(win::on_subspace(*client, 2));
        QVERIFY(win::on_subspace(*client, 3));
        QCOMPARE(client->topo.subspaces.size(), 3u);

        // entering twice dooes nothing
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 3));
        QCOMPARE(client->topo.subspaces.size(), 3u);

        // adding to "all subspaces" results in just that one subspace
        win::set_on_all_subspaces(*client, true);
        QCOMPARE(client->topo.subspaces.size(), 0u);
        win::enter_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 3));
        QVERIFY(win::on_subspace(*client, 3));
        QCOMPARE(client->topo.subspaces.size(), 1u);

        // leaving a subspace on "all subspaces" puts on everything else
        win::set_on_all_subspaces(*client, true);
        QCOMPARE(client->topo.subspaces.size(), 0u);
        win::leave_subspace(*client, win::subspaces_get_for_x11id(*vd_manager, 3));
        QVERIFY(win::on_subspace(*client, 1));
        QVERIFY(win::on_subspace(*client, 2));
        QCOMPARE(client->topo.subspaces.size(), 2u);
    }

    SECTION("remove subspace with window")
    {
        // first create two new subspaces
        QCOMPARE(vd_manager->subspaces.size(), 1u);
        win::subspace_manager_set_count(*vd_manager, 3);
        QCOMPARE(vd_manager->subspaces.size(), 3u);

        // switch to last subspace
        win::subspaces_set_current(*vd_manager, *vd_manager->subspaces.back());
        QCOMPARE(win::subspaces_get_current_x11id(*vd_manager), 3u);

        // now create a window on this subspace
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_subspace(*client), 3u);
        QCOMPARE(client->topo.subspaces.size(), 1u);
        QCOMPARE(vd_manager->current, client->topo.subspaces.front());

        // Set the window on subspace 2 as well
        win::enter_subspace(*client, vd_manager->subspaces[1]);
        QCOMPARE(client->topo.subspaces.size(), 2u);
        QCOMPARE(vd_manager->subspaces.at(2), client->topo.subspaces.at(0));
        QCOMPARE(vd_manager->subspaces.at(1), client->topo.subspaces.at(1));
        QVERIFY(win::on_subspace(*client, 2));
        QVERIFY(win::on_subspace(*client, 3));

        // remove subspace 3
        win::subspace_manager_set_count(*vd_manager, 2);
        QCOMPARE(client->topo.subspaces.size(), 1u);
        // window is only on subspace 2
        QCOMPARE(vd_manager->subspaces[1], client->topo.subspaces.at(0));

        // Again 3 subspaces
        win::subspace_manager_set_count(*vd_manager, 3);
        // move window to be only on subspace 3
        win::enter_subspace(*client, vd_manager->subspaces.at(2));
        win::leave_subspace(*client, vd_manager->subspaces.at(1));
        QCOMPARE(client->topo.subspaces.size(), 1u);
        // window is only on subspace 3
        QCOMPARE(vd_manager->subspaces.at(2), client->topo.subspaces.at(0));

        // remove subspace 3
        win::subspace_manager_set_count(*vd_manager, 2);
        QCOMPARE(client->topo.subspaces.size(), 1u);
        // window is only on subspace 2
        QCOMPARE(vd_manager->subspaces.at(1), client->topo.subspaces.at(0));
    }
}

}
