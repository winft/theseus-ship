/*
SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
    render::full_repaint(*Test::app()->base->render->compositor);

    // now change buffer size
    Test::render(surface, QSize(30, 10), Qt::red);

    QSignalSpy damagedSpy(client->qobject.get(), &win::window_qobject::damaged);
    QVERIFY(damagedSpy.isValid());
    QVERIFY(damagedSpy.wait());
    render::full_repaint(*Test::app()->base->render->compositor);
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
    render::full_repaint(*Test::app()->base->render->compositor);

    // change buffer size of sub surface
    QSignalSpy damagedParentSpy(parent->qobject.get(), &win::window_qobject::damaged);
    QVERIFY(damagedParentSpy.isValid());
    Test::render(surface, QSize(20, 10), Qt::red);
    parentSurface->commit(Surface::CommitFlag::None);

    QVERIFY(damagedParentSpy.wait());
    QTRY_COMPARE(damagedParentSpy.count(), 2);

    // add a second repaint
    render::full_repaint(*Test::app()->base->render->compositor);
}

}

WAYLANDTEST_MAIN(KWin::BufferSizeChangeTest)
#include "buffer_size_change_test.moc"
