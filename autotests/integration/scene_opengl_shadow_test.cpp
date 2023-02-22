/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/shadow.h"
#include "render/window.h"
#include "win/deco.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>
#include <QByteArray>
#include <QDir>
#include <QVector>
#include <Wrapland/Client/shadow.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/surface.h>
#include <algorithm>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

namespace
{

constexpr int SHADOW_SIZE = 128;

constexpr int SHADOW_OFFSET_TOP = 64;
constexpr int SHADOW_OFFSET_LEFT = 48;

// NOTE: We assume deco shadows are generated with blur so that's
//       why there is 4, 1 is the size of the inner shadow rect.
constexpr int SHADOW_TEXTURE_WIDTH = 4 * SHADOW_SIZE + 1;
constexpr int SHADOW_TEXTURE_HEIGHT = 4 * SHADOW_SIZE + 1;

constexpr int SHADOW_PADDING_TOP = SHADOW_SIZE - SHADOW_OFFSET_TOP;
constexpr int SHADOW_PADDING_RIGHT = SHADOW_SIZE + SHADOW_OFFSET_LEFT;
constexpr int SHADOW_PADDING_BOTTOM = SHADOW_SIZE + SHADOW_OFFSET_TOP;
constexpr int SHADOW_PADDING_LEFT = SHADOW_SIZE - SHADOW_OFFSET_LEFT;

constexpr QRectF SHADOW_INNER_RECT(2 * SHADOW_SIZE, 2 * SHADOW_SIZE, 1, 1);

bool isClose(double a, double b, double eps = 1e-5)
{
    if (a == b) {
        return true;
    }
    const double diff = std::fabs(a - b);
    if (a == 0 || b == 0) {
        return diff < eps;
    }
    return diff / std::max(a, b) < eps;
}

bool compareQuads(const WindowQuad& a, const WindowQuad& b)
{
    for (int i = 0; i < 4; i++) {
        if (!isClose(a[i].x(), b[i].x()) || !isClose(a[i].y(), b[i].y())
            || !isClose(a[i].u(), b[i].u()) || !isClose(a[i].v(), b[i].v())) {
            return false;
        }
    }
    return true;
}

WindowQuad makeShadowQuad(QRectF const& geo, qreal tx1, qreal ty1, qreal tx2, qreal ty2)
{
    WindowQuad quad(WindowQuadShadow);
    quad[0] = WindowVertex(geo.left(), geo.top(), tx1, ty1);
    quad[1] = WindowVertex(geo.right(), geo.top(), tx2, ty1);
    quad[2] = WindowVertex(geo.right(), geo.bottom(), tx2, ty2);
    quad[3] = WindowVertex(geo.left(), geo.bottom(), tx1, ty2);
    return quad;
}

}

TEST_CASE("opengl shadow", "[render]")
{
    qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    test::setup setup("opengl-shadow");

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render->compositor).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    QVERIFY(setup.base->render->compositor);

    // Add directory with fake decorations to the plugin search path.
    QCoreApplication::addLibraryPath(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("fakes"));

    // Change decoration theme.
    auto group = setup.base->config.main->group("org.kde.kdecoration2");
    group.writeEntry("library", "org.kde.test.fakedecowithshadows");
    group.sync();
    win::space_reconfigure(*setup.base->space);

    auto& scene = setup.base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);

    SECTION("tile overlaps")
    {
        struct data {
            QSize window_size;
            WindowQuadList expected_quads;
        };

        // Precompute shadow tile geometries(in texture's space).
        QRectF const topLeftTile(0, 0, SHADOW_INNER_RECT.x(), SHADOW_INNER_RECT.y());
        QRectF const topRightTile(SHADOW_INNER_RECT.right(),
                                  0,
                                  SHADOW_TEXTURE_WIDTH - SHADOW_INNER_RECT.right(),
                                  SHADOW_INNER_RECT.y());
        QRectF const topTile(topLeftTile.topRight(), topRightTile.bottomLeft());

        QRectF const bottomLeftTile(0,
                                    SHADOW_INNER_RECT.bottom(),
                                    SHADOW_INNER_RECT.x(),
                                    SHADOW_TEXTURE_HEIGHT - SHADOW_INNER_RECT.bottom());
        QRectF const bottomRightTile(SHADOW_INNER_RECT.right(),
                                     SHADOW_INNER_RECT.bottom(),
                                     SHADOW_TEXTURE_WIDTH - SHADOW_INNER_RECT.right(),
                                     SHADOW_TEXTURE_HEIGHT - SHADOW_INNER_RECT.bottom());
        QRectF const bottomTile(bottomLeftTile.topRight(), bottomRightTile.bottomLeft());

        QRectF const leftTile(topLeftTile.bottomLeft(), bottomLeftTile.topRight());
        QRectF const rightTile(topRightTile.bottomLeft(), bottomRightTile.topRight());

        qreal tx1 = 0;
        qreal ty1 = 0;
        qreal tx2 = 0;
        qreal ty2 = 0;

        // Explanation behind numbers: (256+1 x 256+1) is the minimum window size
        // which doesn't cause overlapping of shadow tiles. For example, if a window
        // has (256 x 256+1) size, top-left and top-right or bottom-left and
        // bottom-right shadow tiles overlap.

        auto get_no_overlap_data = [&]() -> data {
            // No overlaps: In this case corner tiles are rendered as they are,
            // and top/right/bottom/left tiles are stretched.
            QSize const window_size(256 + 1, 256 + 1);
            WindowQuadList shadow_quads;

            QRectF const outerRect(-SHADOW_PADDING_LEFT,
                                   -SHADOW_PADDING_TOP,
                                   window_size.width() + SHADOW_PADDING_LEFT + SHADOW_PADDING_RIGHT,
                                   window_size.height() + SHADOW_PADDING_TOP
                                       + SHADOW_PADDING_BOTTOM);

            QRectF const topLeft(
                outerRect.left(), outerRect.top(), topLeftTile.width(), topLeftTile.height());
            tx1 = topLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topLeftTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            QRectF const topRight(outerRect.right() - topRightTile.width(),
                                  outerRect.top(),
                                  topRightTile.width(),
                                  topRightTile.height());
            tx1 = topRightTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            QRectF const top(topLeft.topRight(), topRight.bottomLeft());
            tx1 = topTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(top, tx1, ty1, tx2, ty2);

            QRectF const bottomLeft(outerRect.left(),
                                    outerRect.bottom() - bottomLeftTile.height(),
                                    bottomLeftTile.width(),
                                    bottomLeftTile.height());
            tx1 = bottomLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = bottomLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = bottomLeftTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            QRectF const bottomRight(outerRect.right() - bottomRightTile.width(),
                                     outerRect.bottom() - bottomRightTile.height(),
                                     bottomRightTile.width(),
                                     bottomRightTile.height());
            tx1 = bottomRightTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = bottomRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = bottomRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const bottom(bottomLeft.topRight(), bottomRight.bottomLeft());
            tx1 = bottomTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = bottomTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = bottomTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottom, tx1, ty1, tx2, ty2);

            QRectF const left(topLeft.bottomLeft(), bottomLeft.topRight());
            tx1 = leftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = leftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = leftTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = leftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(left, tx1, ty1, tx2, ty2);

            QRectF const right(topRight.bottomLeft(), bottomRight.topRight());
            tx1 = rightTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = rightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = rightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = rightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(right, tx1, ty1, tx2, ty2);

            return {window_size, shadow_quads};
        };

        auto get_vert_overlap_data = [&](QSize const& window_size) -> data {
            // Top-Left & Bottom-Left/Top-Right & Bottom-Right overlap: In this case overlapping
            // parts are clipped and left/right tiles aren't rendered.
            WindowQuadList shadow_quads;
            qreal halfOverlap = 0.0;

            QRectF const outerRect(-SHADOW_PADDING_LEFT,
                                   -SHADOW_PADDING_TOP,
                                   window_size.width() + SHADOW_PADDING_LEFT + SHADOW_PADDING_RIGHT,
                                   window_size.height() + SHADOW_PADDING_TOP
                                       + SHADOW_PADDING_BOTTOM);

            QRectF topLeft(
                outerRect.left(), outerRect.top(), topLeftTile.width(), topLeftTile.height());

            QRectF bottomLeft(outerRect.left(),
                              outerRect.bottom() - bottomLeftTile.height(),
                              bottomLeftTile.width(),
                              bottomLeftTile.height());

            halfOverlap = qAbs(topLeft.bottom() - bottomLeft.top()) / 2;
            topLeft.setBottom(topLeft.bottom() - halfOverlap);
            bottomLeft.setTop(bottomLeft.top() + halfOverlap);

            tx1 = topLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topLeftTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topLeft.height() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = bottomLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = 1.0 - (bottomLeft.height() / SHADOW_TEXTURE_HEIGHT);
            tx2 = bottomLeftTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            QRectF topRight(outerRect.right() - topRightTile.width(),
                            outerRect.top(),
                            topRightTile.width(),
                            topRightTile.height());

            QRectF bottomRight(outerRect.right() - bottomRightTile.width(),
                               outerRect.bottom() - bottomRightTile.height(),
                               bottomRightTile.width(),
                               bottomRightTile.height());

            halfOverlap = qAbs(topRight.bottom() - bottomRight.top()) / 2;
            topRight.setBottom(topRight.bottom() - halfOverlap);
            bottomRight.setTop(bottomRight.top() + halfOverlap);

            tx1 = topRightTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topRight.height() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            tx1 = bottomRightTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = 1.0 - (bottomRight.height() / SHADOW_TEXTURE_HEIGHT);
            tx2 = bottomRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const top(topLeft.topRight(), topRight.bottomLeft());
            tx1 = topTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = top.height() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(top, tx1, ty1, tx2, ty2);

            QRectF const bottom(bottomLeft.topRight(), bottomRight.bottomLeft());
            tx1 = bottomTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = 1.0 - (bottom.height() / SHADOW_TEXTURE_HEIGHT);
            tx2 = bottomTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottom, tx1, ty1, tx2, ty2);

            return {window_size, shadow_quads};
        };

        auto get_hor_overlap_data = [&](QSize const& window_size) -> data {
            // Top-Left & Top-Right/Bottom-Left & Bottom-Right overlap: In this case overlapping
            // parts are clipped and top/bottom tiles aren't rendered.
            WindowQuadList shadow_quads;
            qreal halfOverlap = 0.0;

            QRectF const outerRect(-SHADOW_PADDING_LEFT,
                                   -SHADOW_PADDING_TOP,
                                   window_size.width() + SHADOW_PADDING_LEFT + SHADOW_PADDING_RIGHT,
                                   window_size.height() + SHADOW_PADDING_TOP
                                       + SHADOW_PADDING_BOTTOM);

            QRectF topLeft(
                outerRect.left(), outerRect.top(), topLeftTile.width(), topLeftTile.height());

            QRectF topRight(outerRect.right() - topRightTile.width(),
                            outerRect.top(),
                            topRightTile.width(),
                            topRightTile.height());

            halfOverlap = qAbs(topLeft.right() - topRight.left()) / 2;
            topLeft.setRight(topLeft.right() - halfOverlap);
            topRight.setLeft(topRight.left() + halfOverlap);

            tx1 = topLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topLeft.width() / SHADOW_TEXTURE_WIDTH;
            ty2 = topLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = 1.0 - (topRight.width() / SHADOW_TEXTURE_WIDTH);
            ty1 = topRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            QRectF bottomLeft(outerRect.left(),
                              outerRect.bottom() - bottomLeftTile.height(),
                              bottomLeftTile.width(),
                              bottomLeftTile.height());

            QRectF bottomRight(outerRect.right() - bottomRightTile.width(),
                               outerRect.bottom() - bottomRightTile.height(),
                               bottomRightTile.width(),
                               bottomRightTile.height());

            halfOverlap = qAbs(bottomLeft.right() - bottomRight.left()) / 2;
            bottomLeft.setRight(bottomLeft.right() - halfOverlap);
            bottomRight.setLeft(bottomRight.left() + halfOverlap);

            tx1 = bottomLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = bottomLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = bottomLeft.width() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            tx1 = 1.0 - (bottomRight.width() / SHADOW_TEXTURE_WIDTH);
            ty1 = bottomRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = bottomRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const left(topLeft.bottomLeft(), bottomLeft.topRight());
            tx1 = leftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = leftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = left.width() / SHADOW_TEXTURE_WIDTH;
            ty2 = leftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(left, tx1, ty1, tx2, ty2);

            QRectF const right(topRight.bottomLeft(), bottomRight.topRight());
            tx1 = 1.0 - (right.width() / SHADOW_TEXTURE_WIDTH);
            ty1 = rightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = rightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = rightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(right, tx1, ty1, tx2, ty2);

            return {window_size, shadow_quads};
        };

        auto get_all_overlap_data = [&](QSize const& window_size) -> data {
            // All shadow tiles overlap: In this case all overlapping parts are clippend and
            // top/right/bottom/left tiles aren't rendered.
            WindowQuadList shadow_quads;
            qreal halfOverlap = 0.0;

            QRectF const outerRect(-SHADOW_PADDING_LEFT,
                                   -SHADOW_PADDING_TOP,
                                   window_size.width() + SHADOW_PADDING_LEFT + SHADOW_PADDING_RIGHT,
                                   window_size.height() + SHADOW_PADDING_TOP
                                       + SHADOW_PADDING_BOTTOM);

            QRectF topLeft(
                outerRect.left(), outerRect.top(), topLeftTile.width(), topLeftTile.height());

            QRectF topRight(outerRect.right() - topRightTile.width(),
                            outerRect.top(),
                            topRightTile.width(),
                            topRightTile.height());

            QRectF bottomLeft(outerRect.left(),
                              outerRect.bottom() - bottomLeftTile.height(),
                              bottomLeftTile.width(),
                              bottomLeftTile.height());

            QRectF bottomRight(outerRect.right() - bottomRightTile.width(),
                               outerRect.bottom() - bottomRightTile.height(),
                               bottomRightTile.width(),
                               bottomRightTile.height());

            halfOverlap = qAbs(topLeft.right() - topRight.left()) / 2;
            topLeft.setRight(topLeft.right() - halfOverlap);
            topRight.setLeft(topRight.left() + halfOverlap);

            halfOverlap = qAbs(bottomLeft.right() - bottomRight.left()) / 2;
            bottomLeft.setRight(bottomLeft.right() - halfOverlap);
            bottomRight.setLeft(bottomRight.left() + halfOverlap);

            halfOverlap = qAbs(topLeft.bottom() - bottomLeft.top()) / 2;
            topLeft.setBottom(topLeft.bottom() - halfOverlap);
            bottomLeft.setTop(bottomLeft.top() + halfOverlap);

            halfOverlap = qAbs(topRight.bottom() - bottomRight.top()) / 2;
            topRight.setBottom(topRight.bottom() - halfOverlap);
            bottomRight.setTop(bottomRight.top() + halfOverlap);

            tx1 = topLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = topLeftTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topLeft.width() / SHADOW_TEXTURE_WIDTH;
            ty2 = topLeft.height() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = 1.0 - (topRight.width() / SHADOW_TEXTURE_WIDTH);
            ty1 = topRightTile.top() / SHADOW_TEXTURE_HEIGHT;
            tx2 = topRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = topRight.height() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            tx1 = bottomLeftTile.left() / SHADOW_TEXTURE_WIDTH;
            ty1 = 1.0 - (bottomLeft.height() / SHADOW_TEXTURE_HEIGHT);
            tx2 = bottomLeft.width() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomLeftTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            tx1 = 1.0 - (bottomRight.width() / SHADOW_TEXTURE_WIDTH);
            ty1 = 1.0 - (bottomRight.height() / SHADOW_TEXTURE_HEIGHT);
            tx2 = bottomRightTile.right() / SHADOW_TEXTURE_WIDTH;
            ty2 = bottomRightTile.bottom() / SHADOW_TEXTURE_HEIGHT;
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            return {window_size, shadow_quads};
        };

        auto test_data
            = GENERATE_COPY(get_no_overlap_data(),
                            // top-left & bottom-left/top-right & bottom-right overlap
                            get_vert_overlap_data({256 + 1, 256}),
                            // top-left & bottom-left/top-right & bottom-right overlap :: pre
                            get_vert_overlap_data({256 + 1, 256 - 1}),
                            // top-left & top-right/bottom-left & bottom-right overlap
                            get_hor_overlap_data({256, 256 + 1}),
                            // top-left & top-right/bottom-left & bottom-right overlap :: pre
                            get_hor_overlap_data({256 - 1, 256 + 1}),
                            // all corner tiles overlap
                            get_all_overlap_data({256, 256}),
                            // all corner tiles overlap :: pre
                            get_all_overlap_data({256 - 1, 256 - 1}),
                            // Window is too small: do not render any shadow tiles.
                            data{{1, 1}, {}});

        Test::setup_wayland_connection(Test::global_selection::xdg_decoration);

        // Create a decorated client.
        auto surface = Test::create_surface();
        auto shellSurface = Test::create_xdg_shell_toplevel(surface);
        Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(),
                                                                            shellSurface.get());

        // Check the client is decorated.
        auto client = Test::render_and_wait_for_shown(surface, test_data.window_size, Qt::blue);
        QVERIFY(client);
        QVERIFY(win::decoration(client));
        auto decoration = win::decoration(client);
        QVERIFY(decoration);

        // If speciefied decoration theme is not found, KWin loads a default one
        // so we have to check whether a client has right decoration.
        auto decoShadow = decoration->shadow();
        QCOMPARE(decoShadow->shadow().size(), QSize(SHADOW_TEXTURE_WIDTH, SHADOW_TEXTURE_HEIGHT));
        QCOMPARE(decoShadow->paddingTop(), SHADOW_PADDING_TOP);
        QCOMPARE(decoShadow->paddingRight(), SHADOW_PADDING_RIGHT);
        QCOMPARE(decoShadow->paddingBottom(), SHADOW_PADDING_BOTTOM);
        QCOMPARE(decoShadow->paddingLeft(), SHADOW_PADDING_LEFT);

        // Get shadow.
        QVERIFY(client->render);
        QVERIFY(client->render->effect);
        auto shadow = client->render->shadow();

        // Validate shadow quads.
        auto const& quads = shadow->shadowQuads();
        QCOMPARE(quads.size(), test_data.expected_quads.size());

        QVector<bool> mask(test_data.expected_quads.size(), false);
        for (auto const& q : quads) {
            for (int i = 0; i < test_data.expected_quads.size(); i++) {
                if (!compareQuads(q, test_data.expected_quads[i])) {
                    continue;
                }
                if (!mask[i]) {
                    mask[i] = true;
                    break;
                } else {
                    FAIL("got a duplicate shadow quad");
                }
            }
        }

        for (const auto& v : qAsConst(mask)) {
            if (!v) {
                FAIL("missed a shadow quad");
            }
        }
    }

    SECTION("no corner tiles")
    {
        // this test verifies that top/right/bottom/left shadow tiles are
        // still drawn even when corner tiles are missing

        Test::setup_wayland_connection(Test::global_selection::shadow);

        // Create a surface.
        std::unique_ptr<Surface> surface(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        auto* client = Test::render_and_wait_for_shown(surface, QSize(512, 512), Qt::blue);
        QVERIFY(client);
        QVERIFY(!win::decoration(client));

        // Render reference shadow texture with the following params:
        //  - shadow size: 128
        //  - inner rect size: 1
        //  - padding: 128
        QImage referenceShadowTexture(256 + 1, 256 + 1, QImage::Format_ARGB32_Premultiplied);
        referenceShadowTexture.fill(Qt::transparent);

        // We don't care about content of the shadow.

        // Submit the shadow to KWin.
        std::unique_ptr<Wrapland::Client::Shadow> clientShadow(
            Test::get_client().interfaces.shadow_manager.get()->createShadow(surface.get()));
        QVERIFY(clientShadow->isValid());

        auto shmPool = Test::get_client().interfaces.shm.get();

        Buffer::Ptr bufferTop
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128, 0, 1, 128)));
        clientShadow->attachTop(bufferTop);

        Buffer::Ptr bufferRight
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128 + 1, 128, 128, 1)));
        clientShadow->attachRight(bufferRight);

        Buffer::Ptr bufferBottom
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128, 128 + 1, 1, 128)));
        clientShadow->attachBottom(bufferBottom);

        Buffer::Ptr bufferLeft
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(0, 128, 128, 1)));
        clientShadow->attachLeft(bufferLeft);

        clientShadow->setOffsets(QMarginsF(128, 128, 128, 128));

        QSignalSpy commit_spy(client->surface, &Wrapland::Server::Surface::committed);
        QVERIFY(commit_spy.isValid());
        clientShadow->commit();
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(commit_spy.wait());

        // Check that we got right shadow from the client.
        QPointer<Wrapland::Server::Shadow> shadowIface = client->surface->state().shadow;
        QVERIFY(client->surface->state().updates & Wrapland::Server::surface_change::shadow);
        QVERIFY(!shadowIface.isNull());
        QCOMPARE(shadowIface->offset().left(), 128.0);
        QCOMPARE(shadowIface->offset().top(), 128.0);
        QCOMPARE(shadowIface->offset().right(), 128.0);
        QCOMPARE(shadowIface->offset().bottom(), 128.0);

        QVERIFY(client->render);
        QVERIFY(client->render->effect);
        auto shadow = client->render->shadow();
        QVERIFY(shadow != nullptr);

        auto const& quads = shadow->shadowQuads();
        QCOMPARE(quads.count(), 4);

        // Shadow size: 128
        // Padding: QMargins(128, 128, 128, 128)
        // Inner rect: QRect(128, 128, 1, 1)
        // Texture size: QSize(257, 257)
        // Window size: QSize(512, 512)
        WindowQuadList expectedQuads;
        expectedQuads << makeShadowQuad(
            QRectF(0, -128, 512, 128), 128.0 / 257.0, 0.0, 129.0 / 257.0, 128.0 / 257.0); // top
        expectedQuads << makeShadowQuad(
            QRectF(512, 0, 128, 512), 129.0 / 257.0, 128.0 / 257.0, 1.0, 129.0 / 257.0); // right
        expectedQuads << makeShadowQuad(
            QRectF(0, 512, 512, 128), 128.0 / 257.0, 129.0 / 257.0, 129.0 / 257.0, 1.0); // bottom
        expectedQuads << makeShadowQuad(
            QRectF(-128, 0, 128, 512), 0.0, 128.0 / 257.0, 128.0 / 257.0, 129.0 / 257.0); // left

        for (auto const& expectedQuad : qAsConst(expectedQuads)) {
            auto it = std::find_if(
                quads.constBegin(), quads.constEnd(), [&expectedQuad](auto const& quad) {
                    return compareQuads(quad, expectedQuad);
                });
            if (it == quads.constEnd()) {
                QString const message
                    = QStringLiteral(
                          "Missing shadow quad (left: %1, top: %2, right: %3, bottom: %4)")
                          .arg(expectedQuad.left())
                          .arg(expectedQuad.top())
                          .arg(expectedQuad.right())
                          .arg(expectedQuad.bottom());
                QByteArray const rawMessage = message.toLocal8Bit().data();
                QFAIL(rawMessage.data());
            }
        }
    }

    SECTION("distribute huge corner tiles")
    {
        // this test verifies that huge corner tiles are distributed correctly

        Test::setup_wayland_connection(Test::global_selection::shadow);

        // Create a surface.
        std::unique_ptr<Surface> surface(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        auto* client = Test::render_and_wait_for_shown(surface, QSize(64, 64), Qt::blue);
        QVERIFY(client);
        QVERIFY(!win::decoration(client));

        // Submit the shadow to KWin.
        std::unique_ptr<Wrapland::Client::Shadow> clientShadow(
            Test::get_client().interfaces.shadow_manager.get()->createShadow(surface.get()));
        QVERIFY(clientShadow->isValid());

        QImage referenceTileTexture(512, 512, QImage::Format_ARGB32_Premultiplied);
        referenceTileTexture.fill(Qt::transparent);

        auto shmPool = Test::get_client().interfaces.shm.get();

        Buffer::Ptr bufferTopLeft = shmPool->createBuffer(referenceTileTexture);
        clientShadow->attachTopLeft(bufferTopLeft);

        Buffer::Ptr bufferTopRight = shmPool->createBuffer(referenceTileTexture);
        clientShadow->attachTopRight(bufferTopRight);

        clientShadow->setOffsets(QMarginsF(256, 256, 256, 0));

        QSignalSpy commit_spy(client->surface, &Wrapland::Server::Surface::committed);
        QVERIFY(commit_spy.isValid());
        clientShadow->commit();
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(commit_spy.wait());

        // Check that we got right shadow from the client.
        QPointer<Wrapland::Server::Shadow> shadowIface = client->surface->state().shadow;
        QVERIFY(!shadowIface.isNull());
        QCOMPARE(shadowIface->offset().left(), 256.0);
        QCOMPARE(shadowIface->offset().top(), 256.0);
        QCOMPARE(shadowIface->offset().right(), 256.0);
        QCOMPARE(shadowIface->offset().bottom(), 0.0);

        QVERIFY(client->render);
        QVERIFY(client->render->effect);
        auto shadow = client->render->shadow();
        QVERIFY(shadow != nullptr);

        WindowQuadList expectedQuads;

        // Top-left quad
        expectedQuads << makeShadowQuad(QRectF(-256, -256, 256 + 32, 256 + 64),
                                        0.0,
                                        0.0,
                                        (256.0 + 32.0) / 1024.0,
                                        (256.0 + 64.0) / 512.0);

        // Top-right quad
        expectedQuads << makeShadowQuad(QRectF(32, -256, 256 + 32, 256 + 64),
                                        1.0 - (256.0 + 32.0) / 1024.0,
                                        0.0,
                                        1.0,
                                        (256.0 + 64.0) / 512.0);

        WindowQuadList const& quads = shadow->shadowQuads();
        QCOMPARE(quads.count(), expectedQuads.count());

        for (auto const& expectedQuad : qAsConst(expectedQuads)) {
            auto it = std::find_if(
                quads.constBegin(), quads.constEnd(), [&expectedQuad](auto const& quad) {
                    return compareQuads(quad, expectedQuad);
                });
            if (it == quads.constEnd()) {
                QString const message
                    = QStringLiteral(
                          "Missing shadow quad (left: %1, top: %2, right: %3, bottom: %4)")
                          .arg(expectedQuad.left())
                          .arg(expectedQuad.top())
                          .arg(expectedQuad.right())
                          .arg(expectedQuad.bottom());
                QByteArray const rawMessage = message.toLocal8Bit().data();
                QFAIL(rawMessage.data());
            }
        }
    }
}

}
