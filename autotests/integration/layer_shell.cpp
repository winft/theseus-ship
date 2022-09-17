/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "render/effects.h"
#include "render/window.h"
#include "win/screen.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/surface.h>

namespace Clt = Wrapland::Client;

constexpr auto output_count = 2;

Q_DECLARE_METATYPE(QMargins)

enum class align {
    center,
    left,
    right,
    top,
    bottom,
};
Q_DECLARE_METATYPE(align)

namespace KWin
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

class layer_shell_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_create();
    void test_geo_data();
    void test_geo();
    void test_output_change();
    void test_popup();
};

wayland_window* get_wayland_window_from_id(uint32_t id)
{
    return dynamic_cast<wayland_window*>(Test::app()->base.space->windows_map.at(id));
}

void layer_shell_test::initTestCase()
{
    qRegisterMetaType<Clt::Output*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());

    auto geometries = std::vector<QRect>{{0, 0, 1000, 500}, {1000, 0, 1000, 500}};
    Test::app()->set_outputs(geometries);
    Test::test_outputs_geometries(geometries);
}

void layer_shell_test::init()
{
    Test::setup_wayland_connection();
    Test::cursor()->set_pos(QPoint(1280, 512));
}

void layer_shell_test::cleanup()
{
    Test::destroy_wayland_connection();
}

Clt::LayerSurfaceV1* create_layer_surface(Clt::Surface* surface,
                                          Clt::Output* output,
                                          Clt::LayerShellV1::layer lay,
                                          std::string domain,
                                          QObject* parent = nullptr)
{
    auto layer_shell = Test::get_client().interfaces.layer_shell.get();
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
 *  Initializes layer surface with configure round-trip.
 */
void init_ack_layer_surface(Clt::Surface* surface, Clt::LayerSurfaceV1* layer_surface)
{
    configure_payload payload;
    init_ack_layer_surface(surface, layer_surface, payload);
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

void layer_shell_test::test_create()
{
    // Tries to create multiple kinds of layer surfaces.
    QSignalSpy window_spy(Test::app()->base.space->qobject.get(),
                          &win::space::qobject_t::wayland_window_added);
    QVERIFY(window_spy.isValid());

    auto surface = std::unique_ptr<Clt::Surface>(Test::create_surface());
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
        create_layer_surface(surface.get(),
                             Test::get_client().interfaces.outputs.at(1).get(),
                             Clt::LayerShellV1::layer::top,
                             ""));

    layer_surface->set_anchor(Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge);

    configure_payload payload;
    init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

    auto const output1_geo = Test::get_output(1)->geometry();
    QCOMPARE(payload.size, output1_geo.size());

    auto render_size = QSize(100, 50);
    Test::render_and_wait_for_shown(surface, render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
    QVERIFY(window);
    QVERIFY(window->isShown());
    QCOMPARE(window->isHiddenInternal(), false);
    QCOMPARE(window->ready_for_painting, true);
    QCOMPARE(window->bit_depth, 32);
    QVERIFY(window->hasAlpha());

    // By default layer surfaces have keyboard interactivity set to none.
    QCOMPARE(Test::app()->base.space->stacking.active, nullptr);

    QVERIFY(!window->isMaximizable());
    QVERIFY(!window->isMovable());
    QVERIFY(!window->isMovableAcrossScreens());
    QVERIFY(!window->isResizable());
    QVERIFY(!window->isInternal());
    QVERIFY(window->render);
    QVERIFY(window->render->effect);
    QVERIFY(!window->render->effect->internalWindow());

    // Surface is centered.
    QCOMPARE(window->frameGeometry(),
             target_geo(output1_geo, render_size, QMargins(), align::center, align::center));

    window_spy.clear();

    auto surface2 = std::unique_ptr<Clt::Surface>(Test::create_surface());
    auto layer_surface2 = std::unique_ptr<Clt::LayerSurfaceV1>(
        create_layer_surface(surface2.get(),
                             Test::get_client().interfaces.outputs.at(1).get(),
                             Clt::LayerShellV1::layer::bottom,
                             ""));

    layer_surface2->set_anchor(Qt::TopEdge | Qt::BottomEdge);
    layer_surface2->set_size(QSize(100, 0));
    layer_surface2->set_keyboard_interactivity(
        Clt::LayerShellV1::keyboard_interactivity::on_demand);

    init_ack_layer_surface(surface2.get(), layer_surface2.get(), payload);

    QCOMPARE(payload.size, QSize(100, output1_geo.height()));

    // We render at half the size. The resulting surface should be centered.
    // Note that this is a bit of an abuse as in the set_size call we specified a different width.
    // The protocol at the moment does not forbid this.
    render_size = payload.size / 2;

    Test::render_and_wait_for_shown(surface2, render_size, Qt::red);
    QVERIFY(!window_spy.isEmpty());

    auto window2 = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
    QVERIFY(window2);
    QVERIFY(window2->isShown());
    QCOMPARE(window2->isHiddenInternal(), false);
    QCOMPARE(window2->ready_for_painting, true);
    QCOMPARE(Test::app()->base.space->stacking.active, window2);

    // Surface is centered.
    QCOMPARE(window2->frameGeometry(),
             target_geo(output1_geo, render_size, QMargins(), align::center, align::center));
}

void layer_shell_test::test_geo_data()
{
    QTest::addColumn<int>("output");
    QTest::addColumn<Qt::Edges>("anchor");
    QTest::addColumn<QSize>("set_size");
    QTest::addColumn<QMargins>("margin");
    QTest::addColumn<QSize>("render_size");
    QTest::addColumn<align>("align_horizontal");
    QTest::addColumn<align>("align_vertical");

    struct anchor {
        Qt::Edges anchor;
        QByteArray text;
        struct {
            align horizontal;
            align vertical;
        } is_mid;
    };

    // All possible combinations of anchors.
    auto const anchors = {
        anchor{Qt::Edges(), "()", align::center, align::center},
        anchor{Qt::Edges(Qt::LeftEdge), "l", align::left, align::center},
        anchor{Qt::Edges(Qt::TopEdge), "t", align::center, align::top},
        anchor{Qt::Edges(Qt::RightEdge), "r", align::right, align::center},
        anchor{Qt::Edges(Qt::BottomEdge), "b", align::center, align::bottom},
        anchor{Qt::LeftEdge | Qt::TopEdge, "lt", align::left, align::top},
        anchor{Qt::TopEdge | Qt::RightEdge, "tr", align::right, align::top},
        anchor{Qt::RightEdge | Qt::BottomEdge, "rb", align::right, align::bottom},
        anchor{Qt::BottomEdge | Qt::LeftEdge, "bl", align::left, align::bottom},
        anchor{Qt::LeftEdge | Qt::RightEdge, "lr", align::center, align::center},
        anchor{Qt::TopEdge | Qt::BottomEdge, "tb", align::center, align::center},
        anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge, "ltr", align::center, align::top},
        anchor{Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge, "trb", align::right, align::center},
        anchor{Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge, "rbl", align::center, align::bottom},
        anchor{Qt::BottomEdge | Qt::LeftEdge | Qt::TopEdge, "blt", align::left, align::center},
        anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge,
               "ltrb",
               align::center,
               align::center},
    };

    struct margin {
        QMargins margin;
        QByteArray text;
    };

    // Some example margins.
    auto const margins = {
        margin{QMargins(), "0,0,0,0"},
        margin{QMargins(0, 1, 2, 3), "0,1,2,3"},
        margin{QMargins(100, 200, 300, 400), "100,200,300,400"},
    };

    auto const set_size = QSize(100, 200);
    auto const render_size = QSize(100, 50);

    for (auto output = 0; output < output_count; output++) {
        for (auto const& anchor : anchors) {
            for (auto const& margin : margins) {
                auto const text = anchor.text + "-anchor|" + margin.text + "-margin|" + "out"
                    + QString::number(output + 1).toUtf8();
                QTest::newRow(text)
                    << output << anchor.anchor << set_size << margin.margin << render_size
                    << anchor.is_mid.horizontal << anchor.is_mid.vertical;
            }
        }
    }
}

void layer_shell_test::test_geo()
{
    // Checks various standard geometries.
    QSignalSpy window_spy(Test::app()->base.space->qobject.get(),
                          &win::space::qobject_t::wayland_window_added);
    QVERIFY(window_spy.isValid());

    QFETCH(int, output);
    auto surface = std::unique_ptr<Clt::Surface>(Test::create_surface());
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
        create_layer_surface(surface.get(),
                             Test::get_client().interfaces.outputs.at(output).get(),
                             Clt::LayerShellV1::layer::top,
                             ""));

    QFETCH(Qt::Edges, anchor);
    QFETCH(QSize, set_size);
    QFETCH(QMargins, margin);
    layer_surface->set_anchor(anchor);
    layer_surface->set_size(set_size);
    layer_surface->set_margin(margin);

    configure_payload payload;
    init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

    QFETCH(QSize, render_size);
    Test::render_and_wait_for_shown(surface, render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
    QVERIFY(window);

    QFETCH(align, align_horizontal);
    QFETCH(align, align_vertical);
    auto geo = target_geo(Test::get_client().interfaces.outputs.at(output)->geometry(),
                          render_size,
                          margin,
                          align_horizontal,
                          align_vertical);
    QCOMPARE(window->frameGeometry(), geo);
}

void layer_shell_test::test_output_change()
{
    // Checks that output changes are handled correctly.
    QSignalSpy window_spy(Test::app()->base.space->qobject.get(),
                          &win::space::qobject_t::wayland_window_added);
    QVERIFY(window_spy.isValid());

    auto const output_geo = QRect(2000, 0, 1000, 500);
    auto wlr_out = wlr_headless_add_output(
        Test::app()->base.backend, output_geo.width(), output_geo.height());
    QCOMPARE(Test::app()->base.outputs.size(), 3);

    Test::app()->base.all_outputs.back()->force_geometry(output_geo);
    base::update_output_topology(Test::app()->base);

    QTRY_COMPARE(Test::get_client().interfaces.outputs.size(), 3);
    QTRY_COMPARE(Test::get_client().interfaces.outputs.at(2)->geometry(), output_geo);

    auto surface = std::unique_ptr<Clt::Surface>(Test::create_surface());
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
        create_layer_surface(surface.get(),
                             Test::get_client().interfaces.outputs.at(2).get(),
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
    Test::render_and_wait_for_shown(surface, render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
    QVERIFY(window);
    QVERIFY(window->isShown());

    // Surface is centered.
    QCOMPARE(window->frameGeometry(),
             target_geo(output_geo, render_size, QMargins(), align::center, align::center));

    QSignalSpy topology_spy(&Test::app()->base, &base::platform::topology_changed);
    QVERIFY(topology_spy.isValid());

    // Now let's change the size of the output.
    auto output_geo2 = output_geo;
    output_geo2.setWidth(800);
    Test::app()->base.all_outputs.back()->force_geometry(output_geo2);
    base::update_output_topology(Test::app()->base);
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

void layer_shell_test::test_popup()
{
    // Checks popup creation.
    QSignalSpy window_spy(Test::app()->base.space->qobject.get(),
                          &win::space::qobject_t::wayland_window_added);
    QVERIFY(window_spy.isValid());

    // First create the layer surface.
    auto surface = Test::create_surface();
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(
        create_layer_surface(surface.get(),
                             Test::get_client().interfaces.outputs.at(1).get(),
                             Clt::LayerShellV1::layer::top,
                             ""));

    layer_surface->set_anchor(Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge);

    configure_payload payload;
    init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

    auto const output1_geo = Test::get_output(1)->geometry();
    QCOMPARE(payload.size, output1_geo.size());

    auto render_size = QSize(100, 50);
    Test::render_and_wait_for_shown(surface, render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = get_wayland_window_from_id(window_spy.first().first().value<quint32>());
    QVERIFY(window);
    QVERIFY(window->isShown());

    // Surface is centered.
    QCOMPARE(window->frameGeometry(),
             target_geo(output1_geo, render_size, QMargins(), align::center, align::center));

    window_spy.clear();

    Clt::XdgPositioner positioner(QSize(50, 40), QRect(0, 0, 5, 10));
    positioner.setAnchorEdge(Qt::BottomEdge | Qt::RightEdge);
    positioner.setGravity(Qt::BottomEdge | Qt::RightEdge);

    auto popup_surface = Test::create_surface();
    auto popup = Test::create_xdg_shell_popup(
        popup_surface, nullptr, positioner, Test::CreationSetup::CreateOnly);
    layer_surface->get_popup(popup.get());
    Test::init_xdg_shell_popup(popup_surface, popup);

    auto server_popup
        = Test::render_and_wait_for_shown(popup_surface, positioner.initialSize(), Qt::blue);
    QVERIFY(server_popup);
    QCOMPARE(server_popup->frameGeometry(),
             QRect(window->frameGeometry().topLeft() + QPoint(5, 10), QSize(50, 40)));
}

}

WAYLANDTEST_MAIN(KWin::layer_shell_test)
#include "layer_shell.moc"
