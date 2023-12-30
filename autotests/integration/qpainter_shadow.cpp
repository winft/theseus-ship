/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>
#include <QByteArray>
#include <QDir>
#include <QImage>
#include <QMarginsF>
#include <QObject>
#include <QPainter>
#include <QPair>
#include <QVector>
#include <Wrapland/Client/shadow.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/surface.h>
#include <algorithm>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>

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
    double const diff = std::fabs(a - b);
    if (a == 0 || b == 0) {
        return diff < eps;
    }
    return diff / std::max(a, b) < eps;
}

bool compareQuads(const WindowQuad& a, const WindowQuad& b)
{
    for (int i = 0; i < 4; i++) {
        if (!isClose(a[i].x(), b[i].x()) || !isClose(a[i].y(), b[i].y())
            || !isClose(a[i].textureX(), b[i].textureX())
            || !isClose(a[i].textureY(), b[i].textureY())) {
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

TEST_CASE("qpainter shadow", "[render]")
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

    test::setup setup("qpainter-shadow");

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup.base->mod.render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    QVERIFY(setup.base->mod.render);

    // Add directory with fake decorations to the plugin search path.
    QCoreApplication::addLibraryPath(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("fakes"));

    // Change decoration theme.
    auto group = setup.base->config.main->group(QStringLiteral("org.kde.kdecoration2"));
    group.writeEntry("library", "org.kde.test.fakedecowithshadows");
    group.sync();
    win::space_reconfigure(*setup.base->mod.space);

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
            // No overlaps: In this case corner tiles are rendered as they are, and
            // top/right/bottom/left tiles are stretched.
            QSize const window_size(256 + 1, 256 + 1);
            WindowQuadList shadow_quads;

            QRectF const outerRect(-SHADOW_PADDING_LEFT,
                                   -SHADOW_PADDING_TOP,
                                   window_size.width() + SHADOW_PADDING_LEFT + SHADOW_PADDING_RIGHT,
                                   window_size.height() + SHADOW_PADDING_TOP
                                       + SHADOW_PADDING_BOTTOM);

            QRectF const topLeft(
                outerRect.left(), outerRect.top(), topLeftTile.width(), topLeftTile.height());
            tx1 = topLeftTile.left();
            ty1 = topLeftTile.top();
            tx2 = topLeftTile.right();
            ty2 = topLeftTile.bottom();
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            QRectF const topRight(outerRect.right() - topRightTile.width(),
                                  outerRect.top(),
                                  topRightTile.width(),
                                  topRightTile.height());
            tx1 = topRightTile.left();
            ty1 = topRightTile.top();
            tx2 = topRightTile.right();
            ty2 = topRightTile.bottom();
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            QRectF const top(topLeft.topRight(), topRight.bottomLeft());
            tx1 = topTile.left();
            ty1 = topTile.top();
            tx2 = topTile.right();
            ty2 = topTile.bottom();
            shadow_quads << makeShadowQuad(top, tx1, ty1, tx2, ty2);

            QRectF const bottomLeft(outerRect.left(),
                                    outerRect.bottom() - bottomLeftTile.height(),
                                    bottomLeftTile.width(),
                                    bottomLeftTile.height());
            tx1 = bottomLeftTile.left();
            ty1 = bottomLeftTile.top();
            tx2 = bottomLeftTile.right();
            ty2 = bottomLeftTile.bottom();
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            QRectF const bottomRight(outerRect.right() - bottomRightTile.width(),
                                     outerRect.bottom() - bottomRightTile.height(),
                                     bottomRightTile.width(),
                                     bottomRightTile.height());
            tx1 = bottomRightTile.left();
            ty1 = bottomRightTile.top();
            tx2 = bottomRightTile.right();
            ty2 = bottomRightTile.bottom();
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const bottom(bottomLeft.topRight(), bottomRight.bottomLeft());
            tx1 = bottomTile.left();
            ty1 = bottomTile.top();
            tx2 = bottomTile.right();
            ty2 = bottomTile.bottom();
            shadow_quads << makeShadowQuad(bottom, tx1, ty1, tx2, ty2);

            QRectF const left(topLeft.bottomLeft(), bottomLeft.topRight());
            tx1 = leftTile.left();
            ty1 = leftTile.top();
            tx2 = leftTile.right();
            ty2 = leftTile.bottom();
            shadow_quads << makeShadowQuad(left, tx1, ty1, tx2, ty2);

            QRectF const right(topRight.bottomLeft(), bottomRight.topRight());
            tx1 = rightTile.left();
            ty1 = rightTile.top();
            tx2 = rightTile.right();
            ty2 = rightTile.bottom();
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
            topLeft.setBottom(topLeft.bottom() - std::floor(halfOverlap));
            bottomLeft.setTop(bottomLeft.top() + std::ceil(halfOverlap));

            tx1 = topLeftTile.left();
            ty1 = topLeftTile.top();
            tx2 = topLeftTile.right();
            ty2 = topLeft.height();
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = bottomLeftTile.left();
            ty1 = SHADOW_TEXTURE_HEIGHT - bottomLeft.height();
            tx2 = bottomLeftTile.right();
            ty2 = bottomLeftTile.bottom();
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
            topRight.setBottom(topRight.bottom() - std::floor(halfOverlap));
            bottomRight.setTop(bottomRight.top() + std::ceil(halfOverlap));

            tx1 = topRightTile.left();
            ty1 = topRightTile.top();
            tx2 = topRightTile.right();
            ty2 = topRight.height();
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            tx1 = bottomRightTile.left();
            ty1 = SHADOW_TEXTURE_HEIGHT - bottomRight.height();
            tx2 = bottomRightTile.right();
            ty2 = bottomRightTile.bottom();
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const top(topLeft.topRight(), topRight.bottomLeft());
            tx1 = topTile.left();
            ty1 = topTile.top();
            tx2 = topTile.right();
            ty2 = top.height();
            shadow_quads << makeShadowQuad(top, tx1, ty1, tx2, ty2);

            QRectF const bottom(bottomLeft.topRight(), bottomRight.bottomLeft());
            tx1 = bottomTile.left();
            ty1 = SHADOW_TEXTURE_HEIGHT - bottom.height();
            tx2 = bottomTile.right();
            ty2 = bottomTile.bottom();
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
            topLeft.setRight(topLeft.right() - std::floor(halfOverlap));
            topRight.setLeft(topRight.left() + std::ceil(halfOverlap));

            tx1 = topLeftTile.left();
            ty1 = topLeftTile.top();
            tx2 = topLeft.width();
            ty2 = topLeftTile.bottom();
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = SHADOW_TEXTURE_WIDTH - topRight.width();
            ty1 = topRightTile.top();
            tx2 = topRightTile.right();
            ty2 = topRightTile.bottom();
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
            bottomLeft.setRight(bottomLeft.right() - std::floor(halfOverlap));
            bottomRight.setLeft(bottomRight.left() + std::ceil(halfOverlap));

            tx1 = bottomLeftTile.left();
            ty1 = bottomLeftTile.top();
            tx2 = bottomLeft.width();
            ty2 = bottomLeftTile.bottom();
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            tx1 = SHADOW_TEXTURE_WIDTH - bottomRight.width();
            ty1 = bottomRightTile.top();
            tx2 = bottomRightTile.right();
            ty2 = bottomRightTile.bottom();
            shadow_quads << makeShadowQuad(bottomRight, tx1, ty1, tx2, ty2);

            QRectF const left(topLeft.bottomLeft(), bottomLeft.topRight());
            tx1 = leftTile.left();
            ty1 = leftTile.top();
            tx2 = left.width();
            ty2 = leftTile.bottom();
            shadow_quads << makeShadowQuad(left, tx1, ty1, tx2, ty2);

            QRectF const right(topRight.bottomLeft(), bottomRight.topRight());
            tx1 = SHADOW_TEXTURE_WIDTH - right.width();
            ty1 = rightTile.top();
            tx2 = rightTile.right();
            ty2 = rightTile.bottom();
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
            topLeft.setRight(topLeft.right() - std::floor(halfOverlap));
            topRight.setLeft(topRight.left() + std::ceil(halfOverlap));

            halfOverlap = qAbs(bottomLeft.right() - bottomRight.left()) / 2;
            bottomLeft.setRight(bottomLeft.right() - std::floor(halfOverlap));
            bottomRight.setLeft(bottomRight.left() + std::ceil(halfOverlap));

            halfOverlap = qAbs(topLeft.bottom() - bottomLeft.top()) / 2;
            topLeft.setBottom(topLeft.bottom() - std::floor(halfOverlap));
            bottomLeft.setTop(bottomLeft.top() + std::ceil(halfOverlap));

            halfOverlap = qAbs(topRight.bottom() - bottomRight.top()) / 2;
            topRight.setBottom(topRight.bottom() - std::floor(halfOverlap));
            bottomRight.setTop(bottomRight.top() + std::ceil(halfOverlap));

            tx1 = topLeftTile.left();
            ty1 = topLeftTile.top();
            tx2 = topLeft.width();
            ty2 = topLeft.height();
            shadow_quads << makeShadowQuad(topLeft, tx1, ty1, tx2, ty2);

            tx1 = SHADOW_TEXTURE_WIDTH - topRight.width();
            ty1 = topRightTile.top();
            tx2 = topRightTile.right();
            ty2 = topRight.height();
            shadow_quads << makeShadowQuad(topRight, tx1, ty1, tx2, ty2);

            tx1 = bottomLeftTile.left();
            ty1 = SHADOW_TEXTURE_HEIGHT - bottomLeft.height();
            tx2 = bottomLeft.width();
            ty2 = bottomLeftTile.bottom();
            shadow_quads << makeShadowQuad(bottomLeft, tx1, ty1, tx2, ty2);

            tx1 = SHADOW_TEXTURE_WIDTH - bottomRight.width();
            ty1 = SHADOW_TEXTURE_HEIGHT - bottomRight.height();
            tx2 = bottomRightTile.right();
            ty2 = bottomRightTile.bottom();
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

        setup_wayland_connection(global_selection::xdg_decoration);

        // Create a decorated client.
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(
            create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly));
        get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(),
                                                                      shellSurface.get());
        init_xdg_shell_toplevel(surface, shellSurface);

        auto client = render_and_wait_for_shown(surface, test_data.window_size, Qt::blue);

        // Check the client is decorated.
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
        QVERIFY(client->render->shadow());
        auto* shadow = client->render->shadow();

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

        for (auto const& v : qAsConst(mask)) {
            if (!v) {
                FAIL("missed a shadow quad");
            }
        }
    }

    SECTION("shadow texture reconstruction")
    {
        setup_wayland_connection(global_selection::shadow);

        // Create a surface.
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto client = render_and_wait_for_shown(surface, QSize(512, 512), Qt::blue);
        QVERIFY(client);
        QVERIFY(!win::decoration(client));

        // Render reference shadow texture with the following params:
        //  - shadow size: 128
        //  - inner rect size: 1
        //  - padding: 128
        QImage referenceShadowTexture(QSize(256 + 1, 256 + 1), QImage::Format_ARGB32_Premultiplied);
        referenceShadowTexture.fill(Qt::transparent);

        QPainter painter(&referenceShadowTexture);
        painter.fillRect(QRect(10, 10, 192, 200), QColor(255, 0, 0, 128));
        painter.fillRect(QRect(128, 30, 10, 180), QColor(0, 0, 0, 30));
        painter.fillRect(QRect(20, 140, 160, 10), QColor(0, 255, 0, 128));

        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.fillRect(QRect(128, 128, 1, 1), Qt::black);
        painter.end();

        // Create shadow.
        std::unique_ptr<Wrapland::Client::Shadow> clientShadow(
            get_client().interfaces.shadow_manager->createShadow(surface.get()));
        QVERIFY(clientShadow->isValid());

        auto shmPool = get_client().interfaces.shm.get();

        Buffer::Ptr bufferTopLeft
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(0, 0, 128, 128)));
        clientShadow->attachTopLeft(bufferTopLeft);

        Buffer::Ptr bufferTop
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128, 0, 1, 128)));
        clientShadow->attachTop(bufferTop);

        Buffer::Ptr bufferTopRight
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128 + 1, 0, 128, 128)));
        clientShadow->attachTopRight(bufferTopRight);

        Buffer::Ptr bufferRight
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128 + 1, 128, 128, 1)));
        clientShadow->attachRight(bufferRight);

        Buffer::Ptr bufferBottomRight
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128 + 1, 128 + 1, 128, 128)));
        clientShadow->attachBottomRight(bufferBottomRight);

        Buffer::Ptr bufferBottom
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(128, 128 + 1, 1, 128)));
        clientShadow->attachBottom(bufferBottom);

        Buffer::Ptr bufferBottomLeft
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(0, 128 + 1, 128, 128)));
        clientShadow->attachBottomLeft(bufferBottomLeft);

        Buffer::Ptr bufferLeft
            = shmPool->createBuffer(referenceShadowTexture.copy(QRect(0, 128, 128, 1)));
        clientShadow->attachLeft(bufferLeft);

        clientShadow->setOffsets(QMarginsF(128, 128, 128, 128));

        // Commit shadow.
        QSignalSpy committed_spy(client->surface, &Wrapland::Server::Surface::committed);
        QVERIFY(committed_spy.isValid());
        clientShadow->commit();
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(committed_spy.wait());
        QVERIFY(client->surface->state().updates & Wrapland::Server::surface_change::shadow);

        // Check whether we've got right shadow.
        auto shadowIface = client->surface->state().shadow;
        QVERIFY(shadowIface);
        QCOMPARE(shadowIface->offset().left(), 128.0);
        QCOMPARE(shadowIface->offset().top(), 128.0);
        QCOMPARE(shadowIface->offset().right(), 128.0);
        QCOMPARE(shadowIface->offset().bottom(), 128.0);

        // Get SceneQPainterShadow's texture.
        QVERIFY(client->render);
        QVERIFY(client->render->shadow());
        auto& shadowTexture
            = static_cast<render::qpainter::shadow<decltype(client->render)::element_type>*>(
                  client->render->shadow())
                  ->shadowTexture();

        QCOMPARE(shadowTexture, referenceShadowTexture);
    }
}

}
