/*
    SPDX-FileCopyrightText: 2012, 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "win/desktop_set.h"
#include "win/screen.h"
#include "win/virtual_desktops.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

template<typename Functor, typename Data>
void test_direction(test::setup& setup, Data const& test_data, std::string const& action_name)
{
    auto& vd_manager = setup.base->space->virtual_desktop_manager;

    vd_manager->setCount(test_data.init_count);
    vd_manager->setRows(2);
    vd_manager->setCurrent(test_data.init_current);

    Functor functor(*vd_manager);
    QCOMPARE(functor(nullptr, test_data.wrap)->x11DesktopNumber(), test_data.result);

    vd_manager->setNavigationWrappingAround(test_data.wrap);

    auto action = vd_manager->qobject->findChild<QAction*>(QString::fromStdString(action_name));
    QVERIFY(action);
    action->trigger();

    QCOMPARE(vd_manager->current(), test_data.result);
    QCOMPARE(functor(test_data.init_current, test_data.wrap), test_data.result);
}

TEST_CASE("virtual desktop", "[win]")
{
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("virtual-desktop", operation_mode);
    setup.start();

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

    setup_wayland_connection();
    auto& vd_manager = setup.base->space->virtual_desktop_manager;
    vd_manager->setCount(1);
    vd_manager->setCurrent(0u);

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
            data{win::virtual_desktop_manager::maximum(),
                 win::virtual_desktop_manager::maximum(),
                 true,
                 false},
            // Above maximum
            data{win::virtual_desktop_manager::maximum() + 1,
                 win::virtual_desktop_manager::maximum(),
                 true,
                 false},
            // Unchanged
            data{2, 2, false, false});

        QCOMPARE(vd_manager->count(), 1);

        // start with a useful desktop count
        auto const count_init_value = 2;
        vd_manager->setCount(count_init_value);

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::virtual_desktop_manager_qobject::countChanged);
        QSignalSpy desktopsRemoved(vd_manager->qobject.get(),
                                   &win::virtual_desktop_manager_qobject::desktopRemoved);

        auto vdToRemove = vd_manager->desktops().last();

        vd_manager->setCount(test_data.request);
        QCOMPARE(vd_manager->count(), test_data.result);
        QCOMPARE(spy.isEmpty(), !test_data.signal);

        if (!spy.isEmpty()) {
            auto arguments = spy.takeFirst();
            QCOMPARE(arguments.count(), 2);
            QCOMPARE(arguments.at(0).type(), QVariant::UInt);
            QCOMPARE(arguments.at(1).type(), QVariant::UInt);
            QCOMPARE(arguments.at(0).toUInt(), count_init_value);
            QCOMPARE(arguments.at(1).toUInt(), test_data.result);
        }

        QCOMPARE(desktopsRemoved.isEmpty(), !test_data.removed_signal);
        if (!desktopsRemoved.isEmpty()) {
            auto arguments = desktopsRemoved.takeFirst();
            QCOMPARE(arguments.count(), 1);
            QCOMPARE(arguments.at(0).value<win::virtual_desktop*>(), vdToRemove);
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

        QCOMPARE(vd_manager->isNavigationWrappingAround(), true);

        // set to init value
        vd_manager->setNavigationWrappingAround(test_data.init);
        QCOMPARE(vd_manager->isNavigationWrappingAround(), test_data.init);

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::virtual_desktop_manager_qobject::navigationWrappingAroundChanged);
        vd_manager->setNavigationWrappingAround(test_data.request);
        QCOMPARE(vd_manager->isNavigationWrappingAround(), test_data.result);
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

        QCOMPARE(vd_manager->current(), 1);

        vd_manager->setCount(test_data.count);
        REQUIRE(vd_manager->setCurrent(test_data.init) == (test_data.init != 1));
        QCOMPARE(vd_manager->current(), test_data.init);

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::virtual_desktop_manager_qobject::currentChanged);

        QCOMPARE(vd_manager->setCurrent(test_data.request), test_data.signal);
        QCOMPARE(vd_manager->current(), test_data.result);
        QCOMPARE(spy.isEmpty(), !test_data.signal);

        if (!spy.isEmpty()) {
            QList<QVariant> arguments = spy.takeFirst();
            QCOMPARE(arguments.count(), 2);
            QCOMPARE(arguments.at(0).type(), QVariant::UInt);
            QCOMPARE(arguments.at(1).type(), QVariant::UInt);
            QCOMPARE(arguments.at(0).toUInt(), test_data.init);
            QCOMPARE(arguments.at(1).toUInt(), test_data.result);
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

        vd_manager->setCount(test_data.init_count);
        vd_manager->setCurrent(test_data.init_current);

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::virtual_desktop_manager_qobject::currentChanged);

        vd_manager->setCount(test_data.request);
        QCOMPARE(vd_manager->current(), test_data.current);
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap
            data{4, 1, true, 2},
            // desktops, no wrap
            data{4, 1, false, 2},
            // desktops at end, wrap
            data{4, 4, true, 1},
            // desktops at end, no wrap
            data{4, 4, false, 4});

        test_direction<win::virtual_desktop_next>(setup, test_data, "Switch to Next Desktop");
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap
            data{4, 3, true, 2},
            // desktops, no wrap
            data{4, 3, false, 2},
            // desktops at start, wrap
            data{4, 1, true, 4},
            // desktops at start, no wrap
            data{4, 1, false, 1});

        test_direction<win::virtual_desktop_previous>(
            setup, test_data, "Switch to Previous Desktop");
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap, 1st row
            data{4, 2, true, 1},
            // desktops, no wrap, 1st row
            data{4, 2, false, 1},
            // desktops, wrap, 2nd row
            data{4, 4, true, 3},
            // desktops, no wrap, 2nd row
            data{4, 4, false, 3},
            // desktops at start, wrap, 1st row
            data{4, 1, true, 2},
            // desktops at start, no wrap, 1st row
            data{4, 1, false, 1},
            // desktops at start, wrap, 2nd row
            data{4, 3, true, 4},
            // desktops at start, no wrap, 2nd row
            data{4, 3, false, 3},
            // non symmetric, start
            data{5, 5, false, 4},
            // non symmetric, end, no wrap
            data{5, 4, false, 4},
            // non symmetric, end, wrap
            data{5, 4, true, 5});

        test_direction<win::virtual_desktop_left>(
            setup, test_data, "Switch One Desktop to the Left");
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap, 1st row
            data{4, 1, true, 2},
            // desktops, no wrap, 1st row
            data{4, 1, false, 2},
            // desktops, wrap, 2nd row
            data{4, 3, true, 4},
            // desktops, no wrap, 2nd row
            data{4, 3, false, 4},
            // desktops at start, wrap, 1st row
            data{4, 2, true, 1},
            // desktops at start, no wrap, 1st row
            data{4, 2, false, 2},
            // desktops at start, wrap, 2nd row
            data{4, 4, true, 3},
            // desktops at start, no wrap, 2nd row
            data{4, 4, false, 4},
            // non symmetric, start
            data{5, 4, true, 5},
            // non symmetric, end, no wrap
            data{5, 5, false, 5},
            // non symmetric, end, wrap
            data{5, 5, true, 4});

        test_direction<win::virtual_desktop_right>(
            setup, test_data, "Switch One Desktop to the Right");
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap, 1st column
            data{4, 3, true, 1},
            // desktops, no wrap, 1st column
            data{4, 3, false, 1},
            // desktops, wrap, 2nd column
            data{4, 4, true, 2},
            // desktops, no wrap, 2nd column
            data{4, 4, false, 2},
            // desktops at start, wrap, 1st column
            data{4, 1, true, 3},
            // desktops at start, no wrap, 1st column
            data{4, 1, false, 1},
            // desktops at start, wrap, 2nd column
            data{4, 2, true, 4},
            // desktops at start, no wrap, 2nd column
            data{4, 2, false, 2});

        test_direction<win::virtual_desktop_above>(setup, test_data, "Switch One Desktop Up");
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
            // one desktop, wrap
            data{1, 1, true, 1},
            // one desktop, no wrap
            data{1, 1, false, 1},
            // desktops, wrap, 1st column
            data{4, 1, true, 3},
            // desktops, no wrap, 1st column
            data{4, 1, false, 3},
            // desktops, wrap, 2nd column
            data{4, 2, true, 4},
            // desktops, no wrap, 2nd column
            data{4, 2, false, 4},
            // desktops at start, wrap, 1st column
            data{4, 3, true, 1},
            // desktops at start, no wrap, 1st column
            data{4, 3, false, 3},
            // desktops at start, wrap, 2nd column
            data{4, 4, true, 2},
            // desktops at start, no wrap, 2nd column
            data{4, 4, false, 4});

        test_direction<win::virtual_desktop_below>(setup, test_data, "Switch One Desktop Down");
    }

    SECTION("update grid")
    {
        struct data {
            unsigned int init_count;
            QSize size;
            Qt::Orientation orientation;
            QPoint coords;
            unsigned int desktop;
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

        vd_manager->setCount(test_data.init_count);

        win::virtual_desktop_grid grid(*vd_manager);

        QCOMPARE(vd_manager->desktops().count(), int(test_data.init_count));

        grid.update(test_data.size, test_data.orientation, vd_manager->desktops());
        QCOMPARE(grid.size(), test_data.size);
        QCOMPARE(grid.width(), test_data.size.width());
        QCOMPARE(grid.height(), test_data.size.height());

        QCOMPARE(grid.at(test_data.coords), vd_manager->desktopForX11Id(test_data.desktop));

        if (test_data.desktop != 0) {
            QCOMPARE(grid.gridCoords(test_data.desktop), test_data.coords);
        }
    }

    SECTION("update layout")
    {
        // call update layout - implicitly through setCount

        struct data {
            unsigned int desktop;
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

        QSignalSpy spy(vd_manager->qobject.get(),
                       &win::virtual_desktop_manager_qobject::layoutChanged);
        QVERIFY(spy.isValid());

        if (test_data.desktop == 1) {
            // Must be changed back and forth from our default so the spy fires.
            vd_manager->setCount(2);
        }

        vd_manager->setCount(test_data.desktop);
        vd_manager->setRows(2);

        QCOMPARE(vd_manager->grid().size(), test_data.result);
        QVERIFY(!spy.empty());

        auto const& arguments = spy.back();
        QCOMPARE(arguments.at(0).toInt(), test_data.result.width());
        QCOMPARE(arguments.at(1).toInt(), test_data.result.height());

        spy.clear();

        // calling update layout again should not change anything
        vd_manager->updateLayout();
        QCOMPARE(vd_manager->grid().size(), test_data.result);
        QCOMPARE(spy.count(), 1);

        auto const& arguments2 = spy.back();
        QCOMPARE(arguments2.at(0).toInt(), test_data.result.width());
        QCOMPARE(arguments2.at(1).toInt(), test_data.result.height());
    }

    SECTION("name")
    {
        struct data {
            unsigned int init_count;
            unsigned int desktop;
            std::string desktop_name;
        };

        auto test_data = GENERATE(data{4, 1, "Desktop 1"},
                                  data{4, 2, "Desktop 2"},
                                  data{4, 3, "Desktop 3"},
                                  data{4, 4, "Desktop 4"},
                                  data{5, 5, "Desktop 5"});

        vd_manager->setCount(test_data.init_count);
        REQUIRE(vd_manager->name(test_data.desktop)
                == QString::fromStdString(test_data.desktop_name));
    }

    SECTION("switch to shortcut")
    {
        vd_manager->setCount(vd_manager->maximum());
        vd_manager->setCurrent(vd_manager->maximum());

        QCOMPARE(vd_manager->current(), vd_manager->maximum());
        //    vd_manager->initShortcuts();
        auto const toDesktop = QStringLiteral("Switch to Desktop %1");

        for (uint i = 1; i <= vd_manager->maximum(); ++i) {
            const QString desktop(toDesktop.arg(i));
            QAction* action = vd_manager->qobject->findChild<QAction*>(desktop);
            QVERIFY2(action, desktop.toUtf8().constData());
            action->trigger();
            QCOMPARE(vd_manager->current(), i);
        }

        // should still be on max
        QCOMPARE(vd_manager->current(), vd_manager->maximum());
    }

    SECTION("change rows")
    {
        vd_manager->setCount(4);
        vd_manager->setRows(4);
        QCOMPARE(vd_manager->rows(), 4);

        vd_manager->setRows(5);
        QCOMPARE(vd_manager->rows(), 4);

        vd_manager->setCount(2);

        // TODO(romangg): Fails when run in Xwayland mode and passes otherwise. The root cause
        //                seems to be the update from root info in
        //                win::virtual_desktop_manager::updateLayout.
        if (operation_mode == base::operation_mode::wayland) {
            REQUIRE(vd_manager->rows() == 2);
        } else {
            REQUIRE(operation_mode == base::operation_mode::xwayland);
            REQUIRE(vd_manager->rows() == 4);
        }
    }

    SECTION("load")
    {
        // No config yet, load should not change anything.
        vd_manager->load();
        QCOMPARE(vd_manager->count(), 1);

        // Empty config should create one desktop.
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        vd_manager->setConfig(config);
        vd_manager->load();
        QCOMPARE(vd_manager->count(), 1);

        // Setting a sensible number.
        config->group("Desktops").writeEntry("Number", 4);
        vd_manager->load();
        QCOMPARE(vd_manager->count(), 4);

        // Setting the config value and reloading should update.
        config->group("Desktops").writeEntry("Number", 5);
        vd_manager->load();
        QCOMPARE(vd_manager->count(), 5);
    }

    SECTION("save")
    {
        vd_manager->setCount(4);

        // No config yet, just to ensure it actually works.
        vd_manager->save();

        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        vd_manager->setConfig(config);

        REQUIRE(config->hasGroup("Desktops"));

        // Now save should create the group "Desktops".
        vd_manager->save();
        QCOMPARE(config->hasGroup("Desktops"), true);

        auto desktops = config->group("Desktops");
        QCOMPARE(desktops.readEntry<int>("Number", 1), 4);
        QCOMPARE(desktops.hasKey("Name_1"), false);
        QCOMPARE(desktops.hasKey("Name_2"), false);
        QCOMPARE(desktops.hasKey("Name_3"), false);
        QCOMPARE(desktops.hasKey("Name_4"), false);
    }

    SECTION("net current desktop")
    {
        if (!setup.base->x11_data.connection) {
            QSKIP("Skipped on Wayland only");
        }

        QCOMPARE(vd_manager->count(), 1u);
        vd_manager->setCount(4);
        QCOMPARE(vd_manager->count(), 4u);

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

        // go to desktop 2
        vd_manager->setCurrent(2);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 1);
        QVERIFY(ok);

        // go to desktop 3
        vd_manager->setCurrent(3);
        currentDesktop = base::x11::xcb::property(setup.base->x11_data.connection,
                                                  0,
                                                  setup.base->x11_data.root_window,
                                                  currentDesktopAtom,
                                                  XCB_ATOM_CARDINAL,
                                                  0,
                                                  1);
        QCOMPARE(currentDesktop.value(0, &ok), 2);
        QVERIFY(ok);

        // go to desktop 4
        vd_manager->setCurrent(4);
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
        vd_manager->setCurrent(1);
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

    SECTION("last desktop removed")
    {
        // first create a new desktop
        QCOMPARE(vd_manager->count(), 1u);
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);

        // switch to last desktop
        vd_manager->setCurrent(vd_manager->desktops().last());
        QCOMPARE(vd_manager->current(), 2u);

        // now create a window on this desktop
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 2);
        QCOMPARE(client->topo.desktops.count(), 1u);
        QCOMPARE(vd_manager->currentDesktop(), client->topo.desktops.constFirst());

        // and remove last desktop
        vd_manager->setCount(1);
        QCOMPARE(vd_manager->count(), 1u);

        // now the client should be moved as well
        QCOMPARE(win::get_desktop(*client), 1);

        QCOMPARE(client->topo.desktops.count(), 1u);
        QCOMPARE(vd_manager->currentDesktop(), client->topo.desktops.constFirst());
    }

    SECTION("window on multiple desktops")
    {
        // first create two new desktops
        QCOMPARE(vd_manager->count(), 1u);
        vd_manager->setCount(3);
        QCOMPARE(vd_manager->count(), 3u);

        // switch to last desktop
        vd_manager->setCurrent(vd_manager->desktops().last());
        QCOMPARE(vd_manager->current(), 3u);

        // now create a window on this desktop
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 3u);
        QCOMPARE(client->topo.desktops.count(), 1u);
        QCOMPARE(vd_manager->currentDesktop(), client->topo.desktops.constFirst());

        // Set the window on desktop 2 as well
        win::enter_desktop(client, vd_manager->desktopForX11Id(2));
        QCOMPARE(client->topo.desktops.count(), 2u);
        QCOMPARE(vd_manager->desktops()[2], client->topo.desktops.at(0));
        QCOMPARE(vd_manager->desktops()[1], client->topo.desktops.at(1));
        QVERIFY(win::on_desktop(client, 2));
        QVERIFY(win::on_desktop(client, 3));

        // leave desktop 3
        win::leave_desktop(client, vd_manager->desktopForX11Id(3));
        QCOMPARE(client->topo.desktops.count(), 1u);
        // leave desktop 2
        win::leave_desktop(client, vd_manager->desktopForX11Id(2));
        QCOMPARE(client->topo.desktops.count(), 0u);
        // we should be on all desktops now
        QVERIFY(win::on_all_desktops(client));
        // put on desktop 1
        win::enter_desktop(client, vd_manager->desktopForX11Id(1));
        QVERIFY(win::on_desktop(client, 1));
        QVERIFY(!win::on_desktop(client, 2));
        QVERIFY(!win::on_desktop(client, 3));
        QCOMPARE(client->topo.desktops.count(), 1u);
        // put on desktop 2
        win::enter_desktop(client, vd_manager->desktopForX11Id(2));
        QVERIFY(win::on_desktop(client, 1));
        QVERIFY(win::on_desktop(client, 2));
        QVERIFY(!win::on_desktop(client, 3));
        QCOMPARE(client->topo.desktops.count(), 2u);
        // put on desktop 3
        win::enter_desktop(client, vd_manager->desktopForX11Id(3));
        QVERIFY(win::on_desktop(client, 1));
        QVERIFY(win::on_desktop(client, 2));
        QVERIFY(win::on_desktop(client, 3));
        QCOMPARE(client->topo.desktops.count(), 3u);

        // entering twice dooes nothing
        win::enter_desktop(client, vd_manager->desktopForX11Id(3));
        QCOMPARE(client->topo.desktops.count(), 3u);

        // adding to "all desktops" results in just that one desktop
        win::set_on_all_desktops(client, true);
        QCOMPARE(client->topo.desktops.count(), 0u);
        win::enter_desktop(client, vd_manager->desktopForX11Id(3));
        QVERIFY(win::on_desktop(client, 3));
        QCOMPARE(client->topo.desktops.count(), 1u);

        // leaving a desktop on "all desktops" puts on everything else
        win::set_on_all_desktops(client, true);
        QCOMPARE(client->topo.desktops.count(), 0u);
        win::leave_desktop(client, vd_manager->desktopForX11Id(3));
        QVERIFY(win::on_desktop(client, 1));
        QVERIFY(win::on_desktop(client, 2));
        QCOMPARE(client->topo.desktops.count(), 2u);
    }

    SECTION("remove desktop with window")
    {
        // first create two new desktops
        QCOMPARE(vd_manager->count(), 1u);
        vd_manager->setCount(3);
        QCOMPARE(vd_manager->count(), 3u);

        // switch to last desktop
        vd_manager->setCurrent(vd_manager->desktops().last());
        QCOMPARE(vd_manager->current(), 3u);

        // now create a window on this desktop
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

        QVERIFY(client);
        QCOMPARE(win::get_desktop(*client), 3u);
        QCOMPARE(client->topo.desktops.count(), 1u);
        QCOMPARE(vd_manager->currentDesktop(), client->topo.desktops.constFirst());

        // Set the window on desktop 2 as well
        win::enter_desktop(client, vd_manager->desktops()[1]);
        QCOMPARE(client->topo.desktops.count(), 2u);
        QCOMPARE(vd_manager->desktops()[2], client->topo.desktops.at(0));
        QCOMPARE(vd_manager->desktops()[1], client->topo.desktops.at(1));
        QVERIFY(win::on_desktop(client, 2));
        QVERIFY(win::on_desktop(client, 3));

        // remove desktop 3
        vd_manager->setCount(2);
        QCOMPARE(client->topo.desktops.count(), 1u);
        // window is only on desktop 2
        QCOMPARE(vd_manager->desktops()[1], client->topo.desktops.at(0));

        // Again 3 desktops
        vd_manager->setCount(3);
        // move window to be only on desktop 3
        win::enter_desktop(client, vd_manager->desktops()[2]);
        win::leave_desktop(client, vd_manager->desktops()[1]);
        QCOMPARE(client->topo.desktops.count(), 1u);
        // window is only on desktop 3
        QCOMPARE(vd_manager->desktops()[2], client->topo.desktops.at(0));

        // remove desktop 3
        vd_manager->setCount(2);
        QCOMPARE(client->topo.desktops.count(), 1u);
        // window is only on desktop 2
        QCOMPARE(vd_manager->desktops()[1], client->topo.desktops.at(0));
    }
}

}
