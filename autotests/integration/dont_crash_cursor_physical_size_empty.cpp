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
#include "kwin_wayland_test.h"
#include "composite.h"
#include "effectloader.h"
#include "cursor.h"
#include "effects.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/geo.h"

#include <KConfigGroup>

#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/output.h>
#include <Wrapland/Server/wl_output.h>

using namespace KWin;
using namespace Wrapland::Client;
static const QString s_socketName = QStringLiteral("wayland_test_kwin_crash_cursor_physical_size_empty-0");

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
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::XdgDecoration);

    screens()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(640, 512));
}

void DontCrashCursorPhysicalSizeEmpty::cleanup()
{
    Test::destroyWaylandConnection();
}

void DontCrashCursorPhysicalSizeEmpty::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    if (!QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons/DMZ-White/index.theme")).isEmpty()) {
        qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    } else {
        // might be vanilla-dmz (e.g. Arch, FreeBSD)
        qputenv("XCURSOR_THEME", QByteArrayLiteral("Vanilla-DMZ"));
    }
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("0"));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
}

void DontCrashCursorPhysicalSizeEmpty::testMoveCursorOverDeco()
{
    // This test ensures that there is no endless recursion if the cursor theme cannot be created
    // a reason for creation failure could be physical size not existing
    // see BUG: 390314
    std::unique_ptr<Surface> surface(Test::createSurface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.get()));
    Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(), shellSurface.get());

    auto c = Test::renderAndWaitForShown(surface.get(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(win::decoration(c));

    // destroy physical size
    Wrapland::Server::Display *display = waylandServer()->display();
    auto output = display->outputs().front()->output();
    output->set_physical_size(QSize(0, 0));
    // and fake a cursor theme change, so that the theme gets recreated
    emit KWin::Cursor::self()->themeChanged();

    KWin::Cursor::setPos(QPoint(c->frameGeometry().center().x(),
                                win::frame_to_client_pos(c, QPoint()).y() / 2));
}

WAYLANDTEST_MAIN(DontCrashCursorPhysicalSizeEmpty)
#include "dont_crash_cursor_physical_size_empty.moc"
