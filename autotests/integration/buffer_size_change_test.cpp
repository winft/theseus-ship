/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "generic_scene_opengl_test.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

namespace KWin
{

class BufferSizeChangeTest : public GenericSceneOpenGLTest
{
    Q_OBJECT
public:
    BufferSizeChangeTest()
        : GenericSceneOpenGLTest(QByteArrayLiteral("O2"))
    {
    }
private Q_SLOTS:
    void init();
    void testShmBufferSizeChange();
    void testShmBufferSizeChangeOnSubSurface();
};

void BufferSizeChangeTest::init()
{
    Test::setup_wayland_connection();
}

void BufferSizeChangeTest::testShmBufferSizeChange()
{
    // This test verifies that an SHM buffer size change is handled correctly

    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // set buffer size
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // add a first repaint
    render::full_repaint(*Test::app()->base.render->compositor);

    // now change buffer size
    Test::render(surface, QSize(30, 10), Qt::red);

    QSignalSpy damagedSpy(client->qobject.get(), &win::window_qobject::damaged);
    QVERIFY(damagedSpy.isValid());
    QVERIFY(damagedSpy.wait());
    render::full_repaint(*Test::app()->base.render->compositor);
}

void BufferSizeChangeTest::testShmBufferSizeChangeOnSubSurface()
{
    using namespace Wrapland::Client;

    // setup parent surface
    std::unique_ptr<Surface> parentSurface(Test::create_surface());
    QVERIFY(parentSurface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(parentSurface));
    QVERIFY(shellSurface);

    // setup sub surface
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<SubSurface> subSurface(Test::create_subsurface(surface, parentSurface));
    QVERIFY(subSurface);

    // set buffer sizes
    Test::render(surface, QSize(30, 10), Qt::red);
    auto parent = Test::render_and_wait_for_shown(parentSurface, QSize(100, 50), Qt::blue);
    QVERIFY(parent);

    // add a first repaint
    render::full_repaint(*Test::app()->base.render->compositor);

    // change buffer size of sub surface
    QSignalSpy damagedParentSpy(parent->qobject.get(), &win::window_qobject::damaged);
    QVERIFY(damagedParentSpy.isValid());
    Test::render(surface, QSize(20, 10), Qt::red);
    parentSurface->commit(Surface::CommitFlag::None);

    QVERIFY(damagedParentSpy.wait());
    QTRY_COMPARE(damagedParentSpy.count(), 2);

    // add a second repaint
    render::full_repaint(*Test::app()->base.render->compositor);
}

}

WAYLANDTEST_MAIN(KWin::BufferSizeChangeTest)
#include "buffer_size_change_test.moc"
