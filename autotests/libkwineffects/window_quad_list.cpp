/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/catch_macros.h"

#include <render/effect/interface/window_quad.h>

#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

namespace
{

WindowQuad makeQuad(QRectF const& r)
{
    WindowQuad quad(WindowQuadContents);
    quad[0] = WindowVertex(r.x(), r.y(), r.x(), r.y());
    quad[1] = WindowVertex(r.x() + r.width(), r.y(), r.x() + r.width(), r.y());
    quad[2] = WindowVertex(
        r.x() + r.width(), r.y() + r.height(), r.x() + r.width(), r.y() + r.height());
    quad[3] = WindowVertex(r.x(), r.y() + r.height(), r.x(), r.y() + r.height());
    return quad;
}

}

TEST_CASE("window quad list", "[effect],[unit]")
{
    SECTION("make grid")
    {
        struct data {
            WindowQuadList orig;
            int quad_size;
            int expected_count;
            WindowQuadList expected;
        };

        WindowQuadList orig;
        WindowQuadList expected;
        auto const empty = data{orig, 10, 0, expected};

        orig.append(makeQuad(QRectF(0, 0, 10, 10)));
        expected.append(makeQuad(QRectF(0, 0, 10, 10)));
        auto const quad_size_too_large = data{orig, 10, 1, expected};

        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 5, 5)));
        expected.append(makeQuad(QRectF(0, 5, 5, 5)));
        expected.append(makeQuad(QRectF(5, 0, 5, 5)));
        expected.append(makeQuad(QRectF(5, 5, 5, 5)));
        auto const regular_grid = data{orig, 5, 4, expected};

        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 9, 9)));
        expected.append(makeQuad(QRectF(0, 9, 9, 1)));
        expected.append(makeQuad(QRectF(9, 0, 1, 9)));
        expected.append(makeQuad(QRectF(9, 9, 1, 1)));
        auto const irregular_grid = data{orig, 9, 4, expected};

        orig.append(makeQuad(QRectF(0, 10, 4, 3)));
        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 4, 4)));
        expected.append(makeQuad(QRectF(0, 4, 4, 4)));
        expected.append(makeQuad(QRectF(0, 8, 4, 2)));
        expected.append(makeQuad(QRectF(0, 10, 4, 2)));
        expected.append(makeQuad(QRectF(0, 12, 4, 1)));
        expected.append(makeQuad(QRectF(4, 0, 4, 4)));
        expected.append(makeQuad(QRectF(4, 4, 4, 4)));
        expected.append(makeQuad(QRectF(4, 8, 4, 2)));
        expected.append(makeQuad(QRectF(8, 0, 2, 4)));
        expected.append(makeQuad(QRectF(8, 4, 2, 4)));
        expected.append(makeQuad(QRectF(8, 8, 2, 2)));
        auto const irregular_grid2 = data{orig, 4, 11, expected};

        auto test_data = GENERATE_COPY(
            empty, quad_size_too_large, regular_grid, irregular_grid, irregular_grid2);

        auto actual = test_data.orig.makeGrid(test_data.quad_size);
        REQUIRE(actual.count() == test_data.expected_count);

        for (auto const& actualQuad : qAsConst(actual)) {
            bool found = false;
            for (auto const& expectedQuad : qAsConst(test_data.expected)) {
                auto vertexTest = [actualQuad, expectedQuad](int index) {
                    auto const& actualVertex = actualQuad[index];
                    auto const& expectedVertex = expectedQuad[index];
                    if (actualVertex.x() != expectedVertex.x())
                        return false;
                    if (actualVertex.y() != expectedVertex.y())
                        return false;
                    if (actualVertex.u() != expectedVertex.u())
                        return false;
                    if (actualVertex.v() != expectedVertex.v())
                        return false;
                    if (actualVertex.originalX() != expectedVertex.originalX())
                        return false;
                    if (actualVertex.originalY() != expectedVertex.originalY())
                        return false;
                    if (actualVertex.textureX() != expectedVertex.textureX())
                        return false;
                    if (actualVertex.textureY() != expectedVertex.textureY())
                        return false;
                    return true;
                };
                found = vertexTest(0) && vertexTest(1) && vertexTest(2) && vertexTest(3);
                if (found) {
                    break;
                }
            }
            REQUIRE(found);
        }
    }

    SECTION("make regular grid")
    {
        struct data {
            WindowQuadList orig;
            int x_subdivisions;
            int y_subdivisions;
            int expected_count;
            WindowQuadList expected;
        };

        WindowQuadList orig;
        WindowQuadList expected;
        auto const empty = data{orig, 1, 1, 0, expected};

        orig.append(makeQuad(QRectF(0, 0, 10, 10)));
        expected.append(makeQuad(QRectF(0, 0, 10, 10)));
        auto const no_split = data{orig, 1, 1, 1, expected};

        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 5, 10)));
        expected.append(makeQuad(QRectF(5, 0, 5, 10)));
        auto const x_split = data{orig, 2, 1, 2, expected};

        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 10, 5)));
        expected.append(makeQuad(QRectF(0, 5, 10, 5)));
        auto const y_split = data{orig, 1, 2, 2, expected};

        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 5, 5)));
        expected.append(makeQuad(QRectF(5, 0, 5, 5)));
        expected.append(makeQuad(QRectF(0, 5, 5, 5)));
        expected.append(makeQuad(QRectF(5, 5, 5, 5)));
        auto const xy_split = data{orig, 2, 2, 4, expected};

        orig.append(makeQuad(QRectF(0, 10, 4, 2)));
        expected.clear();
        expected.append(makeQuad(QRectF(0, 0, 5, 3)));
        expected.append(makeQuad(QRectF(5, 0, 5, 3)));
        expected.append(makeQuad(QRectF(0, 3, 5, 3)));
        expected.append(makeQuad(QRectF(5, 3, 5, 3)));
        expected.append(makeQuad(QRectF(0, 6, 5, 3)));
        expected.append(makeQuad(QRectF(5, 6, 5, 3)));
        expected.append(makeQuad(QRectF(0, 9, 5, 1)));
        expected.append(makeQuad(QRectF(0, 10, 4, 2)));
        expected.append(makeQuad(QRectF(5, 9, 5, 1)));
        auto const multi_quad = data{orig, 2, 4, 9, expected};

        auto test_data = GENERATE_COPY(empty, no_split, x_split, y_split, xy_split, multi_quad);

        auto actual
            = test_data.orig.makeRegularGrid(test_data.x_subdivisions, test_data.y_subdivisions);
        REQUIRE(actual.count() == test_data.expected_count);

        for (auto const& actualQuad : qAsConst(actual)) {
            bool found = false;
            for (auto const& expectedQuad : qAsConst(test_data.expected)) {
                auto vertexTest = [actualQuad, expectedQuad](int index) {
                    auto const& actualVertex = actualQuad[index];
                    auto const& expectedVertex = expectedQuad[index];
                    if (actualVertex.x() != expectedVertex.x())
                        return false;
                    if (actualVertex.y() != expectedVertex.y())
                        return false;
                    if (actualVertex.u() != expectedVertex.u())
                        return false;
                    if (actualVertex.v() != expectedVertex.v())
                        return false;
                    if (actualVertex.originalX() != expectedVertex.originalX())
                        return false;
                    if (actualVertex.originalY() != expectedVertex.originalY())
                        return false;
                    if (actualVertex.textureX() != expectedVertex.textureX())
                        return false;
                    if (actualVertex.textureY() != expectedVertex.textureY())
                        return false;
                    return true;
                };
                found = vertexTest(0) && vertexTest(1) && vertexTest(2) && vertexTest(3);
                if (found) {
                    break;
                }
            }
            REQUIRE(found);
        }
    }
}

}
