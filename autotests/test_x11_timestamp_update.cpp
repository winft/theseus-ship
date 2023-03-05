/*
SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/setup.h"

#include "base/x11/grabs.h"

namespace KWin::detail::test
{

TEST_CASE("x11 timestamp update", "[base],[xwl]")
{
    test::setup setup("x11-timestamp-update", base::operation_mode::xwayland);
    setup.start();

    SECTION("grab after server time")
    {
        // this test tries to grab the X keyboard with a timestamp in future
        // that should fail, but after updating the X11 timestamp, it should
        // work again
        KWin::base::x11::update_time_from_clock(*setup.base);
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), true);
        KWin::base::x11::ungrab_keyboard(setup.base->x11_data.connection);

        // now let's change the timestamp
        KWin::base::x11::advance_time(setup.base->x11_data,
                                      setup.base->x11_data.time + 5 * 60 * 1000);

        // now grab keyboard should fail
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), false);

        // let's update timestamp, now it should work again
        KWin::base::x11::update_time_from_clock(*setup.base);
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), true);
        KWin::base::x11::ungrab_keyboard(setup.base->x11_data.connection);
    }

    SECTION("before last grab time")
    {
        // this test tries to grab the X keyboard with a timestamp before the
        // last grab time on the server. That should fail, but after updating the X11
        // timestamp it should work again

        // first set the grab timestamp
        KWin::base::x11::update_time_from_clock(*setup.base);
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), true);
        KWin::base::x11::ungrab_keyboard(setup.base->x11_data.connection);

        // now go to past
        auto const timestamp = setup.base->x11_data.time;
        KWin::base::x11::set_time(setup.base->x11_data, setup.base->x11_data.time - 5 * 60 * 1000);
        QCOMPARE(setup.base->x11_data.time, timestamp - 5 * 60 * 1000);

        // now grab keyboard should fail
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), false);

        // let's update timestamp, now it should work again
        KWin::base::x11::update_time_from_clock(*setup.base);
        QVERIFY(setup.base->x11_data.time >= timestamp);
        QCOMPARE(KWin::base::x11::grab_keyboard(setup.base->x11_data), true);
        KWin::base::x11::ungrab_keyboard(setup.base->x11_data.connection);
    }
}

}
