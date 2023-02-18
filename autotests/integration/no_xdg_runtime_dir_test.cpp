/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
        auto mode = KWin::base::operation_mode::wayland;
#else
        auto mode = KWin::base::operation_mode::xwayland;

#endif
        KWin::base::app_singleton app_singleton;
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
