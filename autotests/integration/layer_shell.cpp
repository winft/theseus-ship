/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/effects.h"
#include "render/window.h"
#include "win/screen.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

namespace Clt = Wrapland::Client;

namespace KWin::detail::test
{

namespace
{

enum class align {
    center,
    left,
    right,
    top,
    bottom,
};

Clt::LayerSurfaceV1* create_layer_surface(Clt::Surface* surface,
                                          Clt::Output* output,
                                          Clt::LayerShellV1::layer lay,
                                          std::string domain,
                                          QObject* parent = nullptr)
{
    auto layer_shell = get_client().interfaces.layer_shell.get();
    if (!layer_shell) {
        return nullptr;
    }
    auto layer_surface
        = layer_shell->get_layer_surface(surface, output, lay, std::move(domain), parent);
    if (!layer_surface->isValid()) {
        delete layer_surface;
        return nullptr;
    }
    return layer_surface;
}

struct configure_payload {
    QSize size;
    uint32_t serial;
};

/**
 *  Initializes layer surface with configure round-trip.
 *
 *  @arg payload will hold the payload of the configure callback after return.
 */
void init_ack_layer_surface(Clt::Surface* surface,
                            Clt::LayerSurfaceV1* layer_surface,
                            configure_payload& payload)
{
    QSignalSpy configure_spy(layer_surface, &Clt::LayerSurfaceV1::configure_requested);
    QVERIFY(configure_spy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configure_spy.wait());
    QCOMPARE(configure_spy.count(), 1);
    payload.size = configure_spy.last()[0].toSize();
    payload.serial = configure_spy.last()[1].toInt();
    layer_surface->ack_configure(payload.serial);
}

/**
 *  Centers surface in area when not fills out full area.
 */
QRect target_geo(QRect const& area_geo,
                 QSize const& render_size,
                 QMargins const& margin,
                 align align_horizontal,
                 align align_vertical)
{
    QPoint rel_pos;
    switch (align_horizontal) {
    case align::left:
        rel_pos.rx() = margin.left();
        break;
    case align::right:
        rel_pos.rx() = area_geo.width() - render_size.width() - margin.right();
        break;
    case align::center:
    default:
        rel_pos.rx() = area_geo.width() / 2 - render_size.width() / 2;
    };
    switch (align_vertical) {
    case align::top:
        rel_pos.ry() = margin.top();
        break;
    case align::bottom:
        rel_pos.ry() = area_geo.height() - render_size.height() - margin.bottom();
        break;
    case align::center:
    default:
        rel_pos.ry() = area_geo.height() / 2 - render_size.height() / 2;
    };
    return QRect(area_geo.topLeft() + rel_pos, render_size);
}

}

TEST_CASE("layer shell", "[win]")
{
    test::setup setup("layer-shell");
    setup.start();
    auto geometries = std::vector<QRect>{{0, 0, 1000, 500}, {1000, 0, 1000, 500}};
    setup.set_outputs(geometries);
    test_outputs_geometries(geometries);
    cursor()->set_pos(QPoint(1280, 512));

    setup_wayland_connection();

    auto get_wayland_window_from_id
        = [&](uint32_t id) { return get_wayland_window(setup.base->space->windows_map.at(id)); };

    SECTION("create")
    {
        // Tries to create multiple kinds of layer surfaces.
        QSignalSpy window_spy(setup.base->space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
        QVERIFY(window_spy.isValid());

        auto surface = std::unique_ptr<Clt::Surface>(create_surface());
        auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
            create_layer_surface(surface.get(),
                                 get_client().interfaces.outputs.at(1).get(),
                                 Clt::LayerShellV1::layer::top,
                                 ""));

        layer_surface->set_anchor(Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge);

        configure_payload payload;
        init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

        auto const output1_geo = get_output(1)->geometry();
        QCOMPARE(payload.size, output1_geo.size());

        auto render_size = QSize(100, 50);
        render_and_wait_for_shown(surface, render_size, Qt::blue);
        QVERIFY(!window_spy.isEmpty());

        auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
        QVERIFY(window);
        QVERIFY(window->isShown());
        QCOMPARE(window->isHiddenInternal(), false);
        QCOMPARE(window->render_data.ready_for_painting, true);
        QCOMPARE(window->render_data.bit_depth, 32);
        QVERIFY(win::has_alpha(*window));

        // By default layer surfaces have keyboard interactivity set to none.
        QVERIFY(!setup.base->space->stacking.active);

        QVERIFY(!window->isMaximizable());
        QVERIFY(!window->isMovable());
        QVERIFY(!window->isMovableAcrossScreens());
        QVERIFY(!window->isResizable());
        QVERIFY(window->render);
        QVERIFY(window->render->effect);
        QVERIFY(!window->render->effect->internalWindow());

        // Surface is centered.
        QCOMPARE(window->geo.frame,
                 target_geo(output1_geo, render_size, QMargins(), align::center, align::center));

        window_spy.clear();

        auto surface2 = std::unique_ptr<Clt::Surface>(create_surface());
        auto layer_surface2 = std::unique_ptr<Clt::LayerSurfaceV1>(
            create_layer_surface(surface2.get(),
                                 get_client().interfaces.outputs.at(1).get(),
                                 Clt::LayerShellV1::layer::bottom,
                                 ""));

        layer_surface2->set_anchor(Qt::TopEdge | Qt::BottomEdge);
        layer_surface2->set_size(QSize(100, 0));
        layer_surface2->set_keyboard_interactivity(
            Clt::LayerShellV1::keyboard_interactivity::on_demand);

        init_ack_layer_surface(surface2.get(), layer_surface2.get(), payload);

        QCOMPARE(payload.size, QSize(100, output1_geo.height()));

        // We render at half the size. The resulting surface should be centered.
        // Note that this is a bit of an abuse as in the set_size call we specified a different
        // width. The protocol at the moment does not forbid this.
        render_size = payload.size / 2;

        render_and_wait_for_shown(surface2, render_size, Qt::red);
        QVERIFY(!window_spy.isEmpty());

        auto window2 = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
        QVERIFY(window2);
        QVERIFY(window2->isShown());
        QCOMPARE(window2->isHiddenInternal(), false);
        QCOMPARE(window2->render_data.ready_for_painting, true);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window2);

        // Surface is centered.
        QCOMPARE(window2->geo.frame,
                 target_geo(output1_geo, render_size, QMargins(), align::center, align::center));
    }

    SECTION("geo")
    {
        auto output = GENERATE(0, 1);

        struct anchor {
            Qt::Edges edges;
            struct {
                align horizontal;
                align vertical;
            } is_mid;
        };

        auto anchor_data = GENERATE(
            anchor{Qt::Edges(), {align::center, align::center}},
            anchor{Qt::Edges(Qt::LeftEdge), {align::left, align::center}},
            anchor{Qt::Edges(Qt::TopEdge), {align::center, align::top}},
            anchor{Qt::Edges(Qt::RightEdge), {align::right, align::center}},
            anchor{Qt::Edges(Qt::BottomEdge), {align::center, align::bottom}},
            anchor{Qt::LeftEdge | Qt::TopEdge, {align::left, align::top}},
            anchor{Qt::TopEdge | Qt::RightEdge, {align::right, align::top}},
            anchor{Qt::RightEdge | Qt::BottomEdge, {align::right, align::bottom}},
            anchor{Qt::BottomEdge | Qt::LeftEdge, {align::left, align::bottom}},
            anchor{Qt::LeftEdge | Qt::RightEdge, {align::center, align::center}},
            anchor{Qt::TopEdge | Qt::BottomEdge, {align::center, align::center}},
            anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge, {align::center, align::top}},
            anchor{Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge, {align::right, align::center}},
            anchor{Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge, {align::center, align::bottom}},
            anchor{Qt::BottomEdge | Qt::LeftEdge | Qt::TopEdge, {align::left, align::center}},
            anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge,
                   {align::center, align::center}});

        auto margin = GENERATE(QMargins(), QMargins(0, 1, 2, 3), QMargins(100, 200, 300, 400));

        // Checks various standard geometries.
        QSignalSpy window_spy(setup.base->space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
        QVERIFY(window_spy.isValid());

        auto surface = std::unique_ptr<Clt::Surface>(create_surface());
        auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
            create_layer_surface(surface.get(),
                                 get_client().interfaces.outputs.at(output).get(),
                                 Clt::LayerShellV1::layer::top,
                                 ""));

        layer_surface->set_anchor(anchor_data.edges);
        layer_surface->set_size({100, 200});
        layer_surface->set_margin(margin);

        configure_payload payload;
        init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

        auto const render_size = QSize(100, 50);
        render_and_wait_for_shown(surface, render_size, Qt::blue);
        QVERIFY(!window_spy.isEmpty());

        auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
        QVERIFY(window);

        auto geo = target_geo(get_client().interfaces.outputs.at(output)->geometry(),
                              render_size,
                              margin,
                              anchor_data.is_mid.horizontal,
                              anchor_data.is_mid.vertical);
        QCOMPARE(window->geo.frame, geo);
    }

    SECTION("output change")
    {
        // Checks that output changes are handled correctly.
        QSignalSpy window_spy(setup.base->space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
        QVERIFY(window_spy.isValid());

        auto const output_geo = QRect(2000, 0, 1000, 500);
        auto wlr_out
            = wlr_headless_add_output(setup.base->backend, output_geo.width(), output_geo.height());
        QCOMPARE(setup.base->outputs.size(), 3);

        setup.base->all_outputs.back()->force_geometry(output_geo);
        base::update_output_topology(*setup.base);

        QTRY_COMPARE(get_client().interfaces.outputs.size(), 3);
        QTRY_COMPARE(get_client().interfaces.outputs.at(2)->geometry(), output_geo);

        auto surface = std::unique_ptr<Clt::Surface>(create_surface());
        auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
            create_layer_surface(surface.get(),
                                 get_client().interfaces.outputs.at(2).get(),
                                 Clt::LayerShellV1::layer::top,
                                 ""));

        layer_surface->set_size(QSize(0, 50));
        layer_surface->set_anchor(Qt::RightEdge | Qt::LeftEdge);

        QSignalSpy configure_spy(layer_surface.get(), &Clt::LayerSurfaceV1::configure_requested);
        QVERIFY(configure_spy.isValid());

        configure_payload payload;
        init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

        QCOMPARE(payload.size, QSize(output_geo.size().width(), 50));
        QCOMPARE(configure_spy.size(), 1);

        auto render_size = QSize(100, 50);
        render_and_wait_for_shown(surface, render_size, Qt::blue);
        QVERIFY(!window_spy.isEmpty());

        auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
        QVERIFY(window);
        QVERIFY(window->isShown());

        // Surface is centered.
        QCOMPARE(window->geo.frame,
                 target_geo(output_geo, render_size, QMargins(), align::center, align::center));

        QSignalSpy topology_spy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(topology_spy.isValid());

        // Now let's change the size of the output.
        auto output_geo2 = output_geo;
        output_geo2.setWidth(800);
        setup.base->all_outputs.back()->force_geometry(output_geo2);
        base::update_output_topology(*setup.base);
        QCOMPARE(topology_spy.count(), 1);

        QVERIFY(configure_spy.wait());
        payload.size = configure_spy.last()[0].toSize();
        payload.serial = configure_spy.last()[1].toInt();
        layer_surface->ack_configure(payload.serial);
        QCOMPARE(payload.size, QSize(output_geo2.width(), 50));

        QSignalSpy close_spy(layer_surface.get(), &Clt::LayerSurfaceV1::closed);
        QVERIFY(close_spy.isValid());

        wlr_output_destroy(wlr_out);
        QVERIFY(close_spy.wait());
    }

    SECTION("popup")
    {
        // Checks popup creation.
        QSignalSpy window_spy(setup.base->space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
        QVERIFY(window_spy.isValid());

        // First create the layer surface.
        auto surface = create_surface();
        auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
            create_layer_surface(surface.get(),
                                 get_client().interfaces.outputs.at(1).get(),
                                 Clt::LayerShellV1::layer::top,
                                 ""));

        layer_surface->set_anchor(Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge);

        configure_payload payload;
        init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

        auto const output1_geo = get_output(1)->geometry();
        QCOMPARE(payload.size, output1_geo.size());

        auto render_size = QSize(100, 50);
        render_and_wait_for_shown(surface, render_size, Qt::blue);
        QVERIFY(!window_spy.isEmpty());

        auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
        QVERIFY(window);
        QVERIFY(window->isShown());

        // Surface is centered.
        QCOMPARE(window->geo.frame,
                 target_geo(output1_geo, render_size, QMargins(), align::center, align::center));

        window_spy.clear();

        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(50, 40);
        pos_data.anchor.rect = QRect(0, 0, 5, 10);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
        pos_data.gravity = pos_data.anchor.edge;

        auto popup_surface = create_surface();
        auto popup
            = create_xdg_shell_popup(popup_surface, nullptr, pos_data, CreationSetup::CreateOnly);
        layer_surface->get_popup(popup.get());
        init_xdg_shell_popup(popup_surface, popup);

        auto server_popup = render_and_wait_for_shown(popup_surface, pos_data.size, Qt::blue);
        QVERIFY(server_popup);
        QCOMPARE(server_popup->geo.frame,
                 QRect(window->geo.frame.topLeft() + QPoint(5, 10), QSize(50, 40)));
    }
}

}
