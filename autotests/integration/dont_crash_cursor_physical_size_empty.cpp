/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2018 Martin Flöser <mgraesslin@kde.org>

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
#include "render/effect_loader.h"
#include "render/effects.h"
#include "screens.h"
#include "win/geo.h"
#include "workspace.h"

#include <KConfigGroup>

#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/output.h>
#include <Wrapland/Server/wl_output.h>

using namespace Wrapland::Client;

namespace KWin
{

class DontCrashCursorPhysicalSizeEmpty : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    void initTestCase();
    void cleanup();
    void testMoveCursorOverDeco();
};

void DontCrashCursorPhysicalSizeEmpty::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);

    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void DontCrashCursorPhysicalSizeEmpty::cleanup()
{
    Test::destroy_wayland_connection();
}

void DontCrashCursorPhysicalSizeEmpty::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    if (!QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("icons/DMZ-White/index.theme"))
             .isEmpty()) {
        qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    } else {
        // might be vanilla-dmz (e.g. Arch, FreeBSD)
        qputenv("XCURSOR_THEME", QByteArrayLiteral("Vanilla-DMZ"));
    }
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("0"));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void DontCrashCursorPhysicalSizeEmpty::testMoveCursorOverDeco()
{
    // This test ensures that there is no endless recursion if the cursor theme cannot be created
    // a reason for creation failure could be physical size not existing
    // see BUG: 390314
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(),
                                                                        shellSurface.get());

    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(win::decoration(c));

    // destroy physical size
    auto output = waylandServer()->display->outputs().front()->output();
    output->set_physical_size(QSize(0, 0));

    // and fake a cursor theme change, so that the theme gets recreated
    Q_EMIT input::get_cursor()->theme_changed();

    input::get_cursor()->set_pos(
        QPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));
}

}

WAYLANDTEST_MAIN(KWin::DontCrashCursorPhysicalSizeEmpty)
#include "dont_crash_cursor_physical_size_empty.moc"
