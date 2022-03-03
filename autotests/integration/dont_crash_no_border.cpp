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
#include "input/cursor.h"
#include "render/compositor.h"
#include "render/scene.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/space.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <KDecoration2/Decoration>

#include <linux/input.h>

namespace KWin
{

class DontCrashNoBorder : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testCreateWindow();
};

void DontCrashNoBorder::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("org.kde.kdecoration2").writeEntry("NoPlugin", true);
    config->sync();
    kwinApp()->setConfig(config);

    // this test needs to enforce OpenGL compositing to get into the crashy condition
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();

    auto scene = render::compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
}

void DontCrashNoBorder::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void DontCrashNoBorder::cleanup()
{
    Test::destroy_wayland_connection();
}

void DontCrashNoBorder::testCreateWindow()
{
    // create a window and ensure that this doesn't crash
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));
    QVERIFY(shellSurface);

    auto deco = Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
        shellSurface.get(), shellSurface.get());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    deco->setMode(XdgDecoration::Mode::ServerSide);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface, shellSurface);

    // Without server-side decoration available the mode set by the compositor will be client-side.
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);

    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(500, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QVERIFY(!win::decoration(c));
}

}

WAYLANDTEST_MAIN(KWin::DontCrashNoBorder)
#include "dont_crash_no_border.moc"
