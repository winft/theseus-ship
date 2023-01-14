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
#include "render/compositor.h"
#include "render/cursor.h"
#include "render/effect_loader.h"
#include "render/qpainter/scene.h"
#include "win/geo.h"
#include "win/space.h"
#include "win/x11/window.h"

#include <kwineffects/effects_handler.h>

#include <KConfigGroup>
#include <QPainter>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>
#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class SceneQPainterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testStartFrame();
    void testCursorMoving();
    void testWindow();
    void testWindowScaled();
    void testCompositorRestart();
    void testX11Window();
};

using qpainter_scene_t = render::qpainter::scene<decltype(Test::app()->base.render)::element_type>;

void SceneQPainterTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void SceneQPainterTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = Test::app()->base.config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*effects, *Test::app()->base.render->compositor)
                                  .listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    if (!QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("icons/DMZ-White/index.theme"))
             .isEmpty()) {
        qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    } else {
        // might be vanilla-dmz (e.g. Arch, FreeBSD)
        qputenv("XCURSOR_THEME", QByteArrayLiteral("Vanilla-DMZ"));
    }
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("Q"));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
    QVERIFY(Test::app()->base.render->compositor);
}

void SceneQPainterTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::seat);
}

void SceneQPainterTest::testStartFrame()
{
    // this test verifies that the initial rendering is correct
    render::full_repaint(*Test::app()->base.render->compositor);
    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);
    QCOMPARE(Test::app()->base.render->selected_compositor(), QPainterCompositing);

    // now let's render a reference image for comparison
    QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
    referenceImage.fill(Qt::black);

    QPainter p(&referenceImage);
    auto& sw_cursor = Test::app()->base.render->compositor->software_cursor;
    auto const cursorImage = sw_cursor->image();

    QVERIFY(!cursorImage.isNull());
    p.drawImage(Test::cursor()->pos() - sw_cursor->hotspot(), cursorImage);
    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));
}

void SceneQPainterTest::testCursorMoving()
{
    // this test verifies that rendering is correct also after moving the cursor a few times
    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);

    auto surface = Test::create_surface();
    auto xdg_shell = Test::create_xdg_shell_toplevel(surface);

    QSignalSpy frameRenderedSpy(surface.get(), &Wrapland::Client::Surface::frameRendered);
    QVERIFY(frameRenderedSpy.isValid());

    QVERIFY(Test::render_and_wait_for_shown(surface, QSize(1, 1), Qt::transparent));
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    auto cursor = Test::cursor();

    cursor->set_pos(0, 0);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    cursor->set_pos(10, 0);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    cursor->set_pos(10, 12);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    cursor->set_pos(12, 14);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    cursor->set_pos(50, 60);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    cursor->set_pos(45, 45);
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    // now let's render a reference image for comparison
    QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
    referenceImage.fill(Qt::black);
    QPainter p(&referenceImage);
    auto& sw_cursor = Test::app()->base.render->compositor->software_cursor;
    auto const cursorImage = sw_cursor->image();

    QVERIFY(!cursorImage.isNull());
    p.drawImage(QPoint(45, 45) - sw_cursor->hotspot(), cursorImage);
    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));
}

void SceneQPainterTest::testWindow()
{
    auto cursor = Test::cursor();
    cursor->set_pos(45, 45);
    // this test verifies that a window is rendered correctly
    using namespace Wrapland::Client;

    QVERIFY(Test::wait_for_wayland_pointer());
    std::unique_ptr<Surface> s(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> ss(Test::create_xdg_shell_toplevel(s));
    QVERIFY(s);
    QVERIFY(ss);

    std::unique_ptr<Pointer> p(Test::get_client().interfaces.seat->createPointer());

    QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
    QVERIFY(frameRenderedSpy.isValid());

    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);

    // now let's map the window
    QVERIFY(Test::render_and_wait_for_shown(s, QSize(200, 300), Qt::blue));

    // which should trigger a frame
    s->commit();
    QVERIFY(frameRenderedSpy.wait());

    // we didn't set a cursor image on the surface yet, so it should be just black + window and
    // previous cursor
    QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
    referenceImage.fill(Qt::black);
    QPainter painter(&referenceImage);
    painter.fillRect(0, 0, 200, 300, Qt::blue);

    // now let's set a cursor image
    std::unique_ptr<Surface> cs(Test::create_surface());
    QVERIFY(cs);
    Test::render(cs, QSize(10, 10), Qt::red);
    p->setCursor(cs.get(), QPoint(5, 5));
    s->commit();
    QVERIFY(frameRenderedSpy.wait());
    painter.fillRect(cursor->pos().x() - 5, cursor->pos().y() - 5, 10, 10, Qt::red);

    QEXPECT_FAIL("", "Screen buffer is for unknown reason different with cursor", Continue);
    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));

    // let's move the cursor again
    cursor->set_pos(10, 10);
    s->commit();
    QVERIFY(frameRenderedSpy.wait());
    painter.fillRect(0, 0, 200, 300, Qt::blue);
    painter.fillRect(5, 5, 10, 10, Qt::red);

    QEXPECT_FAIL("", "Screen buffer is for unknown reason different with cursor", Continue);
    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));
}

void SceneQPainterTest::testWindowScaled()
{
    // this test verifies that a window is rendered correctly
    using namespace Wrapland::Client;

    Test::cursor()->set_pos(10, 10);
    QVERIFY(Test::wait_for_wayland_pointer());

    std::unique_ptr<Surface> s(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> ss(Test::create_xdg_shell_toplevel(s));
    QVERIFY(s);
    QVERIFY(ss);

    std::unique_ptr<Pointer> p(Test::get_client().interfaces.seat->createPointer());

    QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
    QVERIFY(frameRenderedSpy.isValid());
    QSignalSpy pointerEnteredSpy(p.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());

    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);

    // now let's set a cursor image
    std::unique_ptr<Surface> cs(Test::create_surface());
    QVERIFY(cs);
    Test::render(cs, QSize(10, 10), Qt::red);

    // now let's map the window
    s->setScale(2);

    // draw a blue square@400x600 with red rectangle@200x200 in the middle
    const QSize size(400, 600);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::blue);
    QPainter surfacePainter(&img);
    surfacePainter.fillRect(200, 300, 200, 200, Qt::red);

    // Add buffer, also commit one more time with default flag to get frame event.
    Test::render(s, img);
    s->commit();
    QVERIFY(pointerEnteredSpy.wait());
    p->setCursor(cs.get(), QPoint(5, 5));

    // which should trigger a frame
    QVERIFY(frameRenderedSpy.wait());
    QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
    referenceImage.fill(Qt::black);
    QPainter painter(&referenceImage);
    painter.fillRect(0, 0, 200, 300, Qt::blue);
    painter.fillRect(100, 150, 100, 100, Qt::red);
    painter.fillRect(5, 5, 10, 10, Qt::red); // cursor

    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));
}

void SceneQPainterTest::testCompositorRestart()
{
    // this test verifies that the compositor/SceneQPainter survive a restart of the compositor and
    // still render correctly
    Test::cursor()->set_pos(400, 400);

    // first create a window
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> s(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> ss(Test::create_xdg_shell_toplevel(s));
    QVERIFY(s);
    QVERIFY(ss);
    QVERIFY(Test::render_and_wait_for_shown(s, QSize(200, 300), Qt::blue));
    s->commit();

    QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
    QVERIFY(frameRenderedSpy.isValid());

    // now let's try to reinitialize the compositing scene
    auto oldScene
        = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(oldScene);

    Test::app()->base.render->compositor->reinitialize();

    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);

    // this should directly trigger a frame
    render::full_repaint(*Test::app()->base.render->compositor);
    QVERIFY(frameRenderedSpy.wait());

    // render reference image
    QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
    referenceImage.fill(Qt::black);
    QPainter painter(&referenceImage);
    painter.fillRect(0, 0, 200, 300, Qt::blue);

    auto& sw_cursor = Test::app()->base.render->compositor->software_cursor;
    auto const cursorImage = sw_cursor->image();
    QVERIFY(!cursorImage.isNull());
    painter.drawImage(QPoint(400, 400) - sw_cursor->hotspot(), cursorImage);
    QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0)));
}

struct XcbConnectionDeleter {
    static inline void cleanup(xcb_connection_t* pointer)
    {
        xcb_disconnect(pointer);
    }
};

void SceneQPainterTest::testX11Window()
{
    // this test verifies the condition of BUG: 382748

    // create X11 window
    QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
    QVERIFY(windowAddedSpy.isValid());

    // Helper window to wait for frame events.
    auto surface = Test::create_surface();
    auto xdg_shell = Test::create_xdg_shell_toplevel(surface);

    QSignalSpy frameRenderedSpy(surface.get(), &Wrapland::Client::Surface::frameRendered);
    QVERIFY(frameRenderedSpy.isValid());

    QVERIFY(Test::render_and_wait_for_shown(surface, QSize(1, 1), Qt::transparent));
    surface->commit();
    QVERIFY(frameRenderedSpy.wait());

    // create an xcb window
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.data());
    uint32_t value = defaultScreen()->white_pixel;
    xcb_create_window(c.data(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_BACK_PIXEL,
                      &value);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client_id = windowCreatedSpy.first().first().value<quint32>();
    auto client = Test::get_x11_window(Test::app()->base.space->windows_map.at(client_id));
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QCOMPARE(win::frame_to_client_size(client, client->geo.size()), QSize(100, 200));

    if (!client->surface) {
        // wait for surface
        QSignalSpy surfaceChangedSpy(client->qobject.get(), &win::window_qobject::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
    }

    QVERIFY(client->surface);

    QSignalSpy committed_spy(client->surface, &Wrapland::Server::Surface::committed);
    QVERIFY(committed_spy.isValid());

    QTRY_VERIFY(client->surface->state().buffer);

    // Xwayland might send one more buffer after the first one with a size of 1x1.
    if (client->surface->state().buffer->size() != client->geo.size()) {
        QTRY_COMPARE(client->surface->state().buffer->size(), QSize(1, 1));
        QVERIFY(committed_spy.wait());
    }

    QTRY_COMPARE(client->surface->state().buffer->size(), client->geo.size());
    QTRY_COMPARE(client->surface->state().buffer->shmImage()->createQImage().size(),
                 client->geo.size());
    QImage compareImage(win::frame_relative_client_rect(client).size(), QImage::Format_RGB32);
    compareImage.fill(Qt::white);
    QCOMPARE(client->surface->state().buffer->shmImage()->createQImage().copy(
                 win::frame_relative_client_rect(client)),
             compareImage);

    // enough time for rendering the window
    QTest::qWait(100);

    // For the frame signal.
    surface->commit();

    auto scene = dynamic_cast<qpainter_scene_t*>(Test::app()->base.render->compositor->scene.get());
    QVERIFY(scene);

    // this should directly trigger a frame
    render::full_repaint(*Test::app()->base.render->compositor);
    QVERIFY(frameRenderedSpy.wait());

    auto const startPos = win::frame_to_client_pos(client, client->geo.pos());
    auto image = scene->backend()->bufferForScreen(Test::app()->base.outputs.at(0));
    QCOMPARE(image->copy(QRect(startPos, win::frame_to_client_size(client, client->geo.size()))),
             compareImage);

    // and destroy the window again
    xcb_unmap_window(c.data(), w);
    xcb_flush(c.data());

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
    xcb_destroy_window(c.data(), w);
    c.reset();
}

}

WAYLANDTEST_MAIN(KWin::SceneQPainterTest)
#include "scene_qpainter_test.moc"
