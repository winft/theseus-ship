/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/setup.h"

#include "win/tabbox/tabbox_desktop_chain.h"

#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("tabbox desktop chain", "[unit],[win]")
{
    SECTION("init")
    {
        struct data {
            int size;
            int next;
            int result;
        };

        auto test_data = GENERATE(data{0, 1, 1},
                                  data{0, 5, 1},
                                  data{1, 1, 1},
                                  data{1, 2, 1},
                                  data{4, 1, 2},
                                  data{4, 2, 3},
                                  data{4, 3, 4},
                                  data{4, 4, 1},
                                  data{4, 5, 1},
                                  data{4, 7, 1});

        win::tabbox_desktop_chain chain(test_data.size);
        REQUIRE(chain.next(test_data.next) == test_data.result);

        win::tabbox_desktop_chain_manager manager;
        manager.resize(0, test_data.size);
        REQUIRE(manager.next(test_data.next) == test_data.result);
    }

    SECTION("add")
    {
        struct data {
            int size;
            int add;
            int next;
            int result;
        };

        auto test_data = GENERATE(
            // invalid size, should not crash
            data{0, 1, 1, 1},
            // moving first element to the front, shouldn't change the chain
            data{4, 1, 1, 2},
            data{4, 1, 2, 3},
            data{4, 1, 3, 4},
            data{4, 1, 4, 1},
            // moving an element from middle to front, should reorder
            data{4, 3, 1, 2},
            data{4, 3, 2, 4},
            data{4, 3, 3, 1},
            data{4, 3, 4, 3},
            // adding an element which does not exist - should leave the chain untouched
            data{4, 5, 1, 2},
            data{4, 5, 2, 3},
            data{4, 5, 3, 4},
            data{4, 5, 4, 1});

        win::tabbox_desktop_chain chain(test_data.size);
        chain.add(test_data.add);
        REQUIRE(chain.next(test_data.next) == test_data.result);

        win::tabbox_desktop_chain_manager manager;
        manager.resize(0, test_data.size);
        manager.add_desktop(0, test_data.add);
        REQUIRE(manager.next(test_data.next) == test_data.result);
    }

    SECTION("resize")
    {
        struct data {
            int size;
            int add;
            int newSize;
            int next;
            int result;
        };

        auto test_data = GENERATE(
            // basic test - increment by one
            data{1, 1, 2, 1, 2},
            data{1, 1, 2, 2, 1},
            // more complex test - increment by three, keep chain untouched
            data{3, 1, 6, 1, 2},
            data{3, 1, 6, 2, 3},
            data{3, 1, 6, 3, 4},
            data{3, 1, 6, 4, 5},
            data{3, 1, 6, 5, 6},
            data{3, 1, 6, 6, 1},
            // increment by three, but change it before
            data{3, 3, 6, 1, 2},
            data{3, 3, 6, 2, 4},
            data{3, 3, 6, 3, 1},
            data{3, 3, 6, 4, 5},
            data{3, 3, 6, 5, 6},
            data{3, 3, 6, 6, 3},
            // basic test - decrement by one
            data{2, 1, 1, 1, 1},
            data{2, 2, 1, 1, 1},
            // more complex test - decrement by three, keep chain untouched
            data{6, 1, 3, 1, 2},
            data{6, 1, 3, 2, 3},
            data{6, 1, 3, 3, 1},
            // more complex test - decrement by three, move element to front
            data{6, 6, 3, 1, 2},
            data{6, 6, 3, 2, 3},
            data{6, 6, 3, 3, 1});

        win::tabbox_desktop_chain chain(test_data.size);
        chain.add(test_data.add);
        chain.resize(test_data.size, test_data.newSize);
        REQUIRE(chain.next(test_data.next) == test_data.result);

        win::tabbox_desktop_chain_manager manager;
        manager.resize(0, test_data.size);
        manager.add_desktop(0, test_data.add);
        manager.resize(test_data.size, test_data.newSize);
        REQUIRE(manager.next(test_data.next) == test_data.result);
    }

    SECTION("resize add")
    {
        // test that verifies that add works after shrinking the chain
        win::tabbox_desktop_chain chain(6);
        win::tabbox_desktop_chain_manager manager;
        manager.resize(0, 6);
        chain.add(4);
        manager.add_desktop(0, 4);
        chain.add(5);
        manager.add_desktop(4, 5);
        chain.add(6);
        manager.add_desktop(5, 6);
        QCOMPARE(chain.next(6), (uint)5);
        QCOMPARE(manager.next(6), (uint)5);
        QCOMPARE(chain.next(5), (uint)4);
        QCOMPARE(manager.next(5), (uint)4);
        QCOMPARE(chain.next(4), (uint)1);
        QCOMPARE(manager.next(4), (uint)1);
        chain.resize(6, 3);
        manager.resize(6, 3);
        QCOMPARE(chain.next(3), (uint)3);
        QCOMPARE(manager.next(3), (uint)3);
        QCOMPARE(chain.next(1), (uint)3);
        QCOMPARE(manager.next(1), (uint)3);
        QCOMPARE(chain.next(2), (uint)3);
        QCOMPARE(manager.next(2), (uint)3);
        // add
        chain.add(1);
        manager.add_desktop(3, 1);
        QCOMPARE(chain.next(3), (uint)3);
        QCOMPARE(manager.next(3), (uint)3);
        QCOMPARE(chain.next(1), (uint)3);
        QCOMPARE(manager.next(1), (uint)3);
        chain.add(2);
        manager.add_desktop(1, 2);
        QCOMPARE(chain.next(1), (uint)3);
        QCOMPARE(manager.next(1), (uint)3);
        QCOMPARE(chain.next(2), (uint)1);
        QCOMPARE(manager.next(2), (uint)1);
        QCOMPARE(chain.next(3), (uint)2);
        QCOMPARE(manager.next(3), (uint)2);
    }

    SECTION("use")
    {
        win::tabbox_desktop_chain_manager manager;
        manager.resize(0, 4);
        manager.add_desktop(0, 3);
        // creating the first chain, should keep it unchanged
        manager.use_chain(QStringLiteral("test"));
        QCOMPARE(manager.next(3), (uint)1);
        QCOMPARE(manager.next(1), (uint)2);
        QCOMPARE(manager.next(2), (uint)4);
        QCOMPARE(manager.next(4), (uint)3);
        // but creating a second chain, should create an empty one
        manager.use_chain(QStringLiteral("second chain"));
        QCOMPARE(manager.next(1), (uint)2);
        QCOMPARE(manager.next(2), (uint)3);
        QCOMPARE(manager.next(3), (uint)4);
        QCOMPARE(manager.next(4), (uint)1);
        // adding a desktop should only affect the currently used one
        manager.add_desktop(3, 2);
        QCOMPARE(manager.next(1), (uint)3);
        QCOMPARE(manager.next(2), (uint)1);
        QCOMPARE(manager.next(3), (uint)4);
        QCOMPARE(manager.next(4), (uint)2);
        // verify by switching back
        manager.use_chain(QStringLiteral("test"));
        QCOMPARE(manager.next(3), (uint)1);
        QCOMPARE(manager.next(1), (uint)2);
        QCOMPARE(manager.next(2), (uint)4);
        QCOMPARE(manager.next(4), (uint)3);
        manager.add_desktop(3, 1);
        // use second chain again and put 4th desktop to front
        manager.use_chain(QStringLiteral("second chain"));
        manager.add_desktop(3, 4);
        // just for the fun a third chain, and let's shrink it
        manager.use_chain(QStringLiteral("third chain"));
        manager.resize(4, 3);
        QCOMPARE(manager.next(1), (uint)2);
        QCOMPARE(manager.next(2), (uint)3);
        // it must have affected all chains
        manager.use_chain(QStringLiteral("test"));
        QCOMPARE(manager.next(1), (uint)3);
        QCOMPARE(manager.next(3), (uint)2);
        QCOMPARE(manager.next(2), (uint)1);
        manager.use_chain(QStringLiteral("second chain"));
        QCOMPARE(manager.next(3), (uint)2);
        QCOMPARE(manager.next(1), (uint)3);
        QCOMPARE(manager.next(2), (uint)1);
    }
}

}
