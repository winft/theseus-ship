/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

namespace KWin::detail::test
{

TEST_CASE("no xdg runtime dir", "[base]")
{
    unsetenv("XDG_RUNTIME_DIR");
    REQUIRE_THROWS([&] { test::setup setup("no-xdg-runtime-dir"); }());
}

}
