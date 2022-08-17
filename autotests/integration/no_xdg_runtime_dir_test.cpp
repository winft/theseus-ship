/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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

namespace KWin
{

class NoXdgRuntimeDirTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void testInitFails();

public:
    bool error_caught{false};
};

void NoXdgRuntimeDirTest::initTestCase()
{
}

void NoXdgRuntimeDirTest::testInitFails()
{
    // this test verifies that without an XDG_RUNTIME_DIR the WaylandServer fials to init
    QVERIFY(error_caught);
}

}

int main(int argc, char* argv[])
{
    unsetenv("XDG_RUNTIME_DIR");

    KWin::NoXdgRuntimeDirTest tc;

    try {
        using namespace KWin;
        Test::prepare_app_env(argv[0]);
#ifdef NO_XWAYLAND
        auto mode = KWin::Application::OperationModeWaylandOnly;
#else
        auto mode = KWin::Application::OperationModeXwayland;

#endif
        auto app = WaylandTestApplication(mode,
                                          Test::create_socket_name("KWin::NoXdgRuntimeDirTest"),
                                          base::wayland::start_options::none,
                                          argc,
                                          argv);
    } catch (...) {
        tc.error_caught = true;
    }

    return QTest::qExec(&tc, argc, argv);
}

#include "no_xdg_runtime_dir_test.moc"
