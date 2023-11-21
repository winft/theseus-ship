/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/catch_macros.h"
#include "mock_tabbox_handler.h"

#include "win/tabbox/tabbox_client_model.h"

namespace KWin::detail::test
{

TEST_CASE("tabbox handler", "[unit],[win]")
{
    SECTION("no crash update outline null client")
    {
        /*
         * Test to verify that update outline does not crash if the ModelIndex for which the outline
         * should be shown is not valid. That is accessing the Pointer to the Client returns an
         * invalid QVariant. BUG: 304620
         */
        MockTabBoxHandler tabboxhandler;
        win::tabbox_config config;
        config.set_show_tabbox(false);
        config.set_highlight_windows(false);
        tabboxhandler.set_config(config);
        // now show the tabbox which will attempt to show the outline
        tabboxhandler.show();
    }
}

}
