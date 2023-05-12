/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

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
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

TEST_CASE("scene qpainter", "[render]")
{
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

    test::setup setup("scene-qpainter", base::operation_mode::xwayland);

    using qpainter_scene_t = render::qpainter::scene<decltype(setup.base->render)::element_type>;

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    setup_wayland_connection(global_selection::seat);

    SECTION("start frame")
    {
        // this test verifies that the initial rendering is correct
        render::full_repaint(*setup.base->render->compositor);
        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);
        QCOMPARE(setup.base->render->selected_compositor(), QPainterCompositing);

        // now let's render a reference image for comparison
        QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
        referenceImage.fill(Qt::black);

        QPainter p(&referenceImage);
        auto& sw_cursor = setup.base->render->compositor->software_cursor;
        auto const cursorImage = sw_cursor->image();

        QVERIFY(!cursorImage.isNull());
        p.drawImage(cursor()->pos() - sw_cursor->hotspot(), cursorImage);
        QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));
    }

    SECTION("cursor moving")
    {
        // this test verifies that rendering is correct also after moving the cursor a few times
        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);

        auto surface = create_surface();
        auto xdg_shell = create_xdg_shell_toplevel(surface);

        QSignalSpy frameRenderedSpy(surface.get(), &Wrapland::Client::Surface::frameRendered);
        QVERIFY(frameRenderedSpy.isValid());

        QVERIFY(render_and_wait_for_shown(surface, QSize(1, 1), Qt::transparent));
        surface->commit();
        QVERIFY(frameRenderedSpy.wait());

        auto cursor = test::cursor();

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
        auto& sw_cursor = setup.base->render->compositor->software_cursor;
        auto const cursorImage = sw_cursor->image();

        QVERIFY(!cursorImage.isNull());
        p.drawImage(QPoint(45, 45) - sw_cursor->hotspot(), cursorImage);
        QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));
    }

    SECTION("window")
    {
        auto cursor = test::cursor();
        cursor->set_pos(45, 45);
        // this test verifies that a window is rendered correctly
        using namespace Wrapland::Client;

        QVERIFY(wait_for_wayland_pointer());
        std::unique_ptr<Surface> s(create_surface());
        std::unique_ptr<XdgShellToplevel> ss(create_xdg_shell_toplevel(s));
        QVERIFY(s);
        QVERIFY(ss);

        std::unique_ptr<Pointer> p(get_client().interfaces.seat->createPointer());

        QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
        QVERIFY(frameRenderedSpy.isValid());

        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);

        // now let's map the window
        QVERIFY(render_and_wait_for_shown(s, QSize(200, 300), Qt::blue));

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
        std::unique_ptr<Surface> cs(create_surface());
        QVERIFY(cs);
        render(cs, QSize(10, 10), Qt::red);
        p->setCursor(cs.get(), QPoint(5, 5));
        s->commit();
        QVERIFY(frameRenderedSpy.wait());
        painter.fillRect(cursor->pos().x() - 5, cursor->pos().y() - 5, 10, 10, Qt::red);

        // TODO(romangg): Screen buffer is for unknown reason different with cursor
        REQUIRE_FALSE(referenceImage
                      == *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));

        // let's move the cursor again
        cursor->set_pos(10, 10);
        s->commit();
        QVERIFY(frameRenderedSpy.wait());
        painter.fillRect(0, 0, 200, 300, Qt::blue);
        painter.fillRect(5, 5, 10, 10, Qt::red);

        // TODO(romangg): Screen buffer is for unknown reason different with cursor
        REQUIRE_FALSE(referenceImage
                      == *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));
    }

    SECTION("window scaled")
    {
        // this test verifies that a window is rendered correctly
        using namespace Wrapland::Client;

        cursor()->set_pos(10, 10);
        QVERIFY(wait_for_wayland_pointer());

        std::unique_ptr<Surface> s(create_surface());
        std::unique_ptr<XdgShellToplevel> ss(create_xdg_shell_toplevel(s));
        QVERIFY(s);
        QVERIFY(ss);

        std::unique_ptr<Pointer> p(get_client().interfaces.seat->createPointer());

        QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
        QVERIFY(frameRenderedSpy.isValid());
        QSignalSpy pointerEnteredSpy(p.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());

        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);

        // now let's set a cursor image
        std::unique_ptr<Surface> cs(create_surface());
        QVERIFY(cs);
        render(cs, QSize(10, 10), Qt::red);

        // now let's map the window
        s->setScale(2);

        // draw a blue square@400x600 with red rectangle@200x200 in the middle
        const QSize size(400, 600);
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::blue);
        QPainter surfacePainter(&img);
        surfacePainter.fillRect(200, 300, 200, 200, Qt::red);

        // Add buffer, also commit one more time with default flag to get frame event.
        render(s, img);
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

        QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));
    }

    SECTION("compositor restart")
    {
        // this test verifies that the compositor/SceneQPainter survive a restart of the compositor
        // and still render correctly
        cursor()->set_pos(400, 400);

        // first create a window
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> s(create_surface());
        std::unique_ptr<XdgShellToplevel> ss(create_xdg_shell_toplevel(s));
        QVERIFY(s);
        QVERIFY(ss);
        QVERIFY(render_and_wait_for_shown(s, QSize(200, 300), Qt::blue));
        s->commit();

        QSignalSpy frameRenderedSpy(s.get(), &Wrapland::Client::Surface::frameRendered);
        QVERIFY(frameRenderedSpy.isValid());

        // now let's try to reinitialize the compositing scene
        auto oldScene
            = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(oldScene);

        setup.base->render->compositor->reinitialize();

        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);

        // this should directly trigger a frame
        render::full_repaint(*setup.base->render->compositor);
        QVERIFY(frameRenderedSpy.wait());

        // render reference image
        QImage referenceImage(QSize(1280, 1024), QImage::Format_RGB32);
        referenceImage.fill(Qt::black);
        QPainter painter(&referenceImage);
        painter.fillRect(0, 0, 200, 300, Qt::blue);

        auto& sw_cursor = setup.base->render->compositor->software_cursor;
        auto const cursorImage = sw_cursor->image();
        QVERIFY(!cursorImage.isNull());
        painter.drawImage(QPoint(400, 400) - sw_cursor->hotspot(), cursorImage);
        QCOMPARE(referenceImage, *scene->backend()->bufferForScreen(setup.base->outputs.at(0)));
    }

    SECTION("x11 window")
    {
        // this test verifies the condition of BUG: 382748

        struct XcbConnectionDeleter {
            static inline void cleanup(xcb_connection_t* pointer)
            {
                xcb_disconnect(pointer);
            }
        };

        // create X11 window
        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());

        // Helper window to wait for frame events.
        auto surface = create_surface();
        auto xdg_shell = create_xdg_shell_toplevel(surface);

        QSignalSpy frameRenderedSpy(surface.get(), &Wrapland::Client::Surface::frameRendered);
        QVERIFY(frameRenderedSpy.isValid());

        QVERIFY(render_and_wait_for_shown(surface, QSize(1, 1), Qt::transparent));
        surface->commit();
        QVERIFY(frameRenderedSpy.wait());

        // create an xcb window
        QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
        QVERIFY(!xcb_connection_has_error(c.data()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.data());
        uint32_t value = base::x11::get_default_screen(setup.base->x11_data)->white_pixel;
        xcb_create_window(c.data(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
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
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &win::space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(win::frame_to_client_size(client, client->geo.size()), QSize(100, 200));

        if (!client->surface) {
            // wait for surface
            QSignalSpy surfaceChangedSpy(client->qobject.get(),
                                         &win::window_qobject::surfaceChanged);
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

        auto scene = dynamic_cast<qpainter_scene_t*>(setup.base->render->compositor->scene.get());
        QVERIFY(scene);

        // this should directly trigger a frame
        render::full_repaint(*setup.base->render->compositor);
        QVERIFY(frameRenderedSpy.wait());

        auto const startPos = win::frame_to_client_pos(client, client->geo.pos());
        auto image = scene->backend()->bufferForScreen(setup.base->outputs.at(0));
        QCOMPARE(
            image->copy(QRect(startPos, win::frame_to_client_size(client, client->geo.size()))),
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

}
