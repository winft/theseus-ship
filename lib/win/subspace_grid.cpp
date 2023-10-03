/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace_grid.h"

namespace KWin::win
{

subspace_grid::subspace_grid()
    : m_size(1, 2) // Default to tow rows
    , m_grid(QVector<QVector<subspace*>>{QVector<subspace*>{}, QVector<subspace*>{}})
{
}

subspace_grid::~subspace_grid() = default;

void subspace_grid::update(QSize const& size,
                           Qt::Orientation orientation,
                           QVector<subspace*> const& subs)
{
    // Set private variables
    m_size = size;
    uint const width = size.width();
    uint const height = size.height();

    m_grid.clear();
    auto it = subs.begin();
    auto end = subs.end();

    if (orientation == Qt::Horizontal) {
        for (uint y = 0; y < height; ++y) {
            QVector<subspace*> row;
            for (uint x = 0; x < width && it != end; ++x) {
                row << *it;
                it++;
            }
            m_grid << row;
        }
    } else {
        for (uint y = 0; y < height; ++y) {
            m_grid << QVector<subspace*>();
        }
        for (uint x = 0; x < width; ++x) {
            for (uint y = 0; y < height && it != end; ++y) {
                auto& row = m_grid[y];
                row << *it;
                it++;
            }
        }
    }
}

QPoint subspace_grid::gridCoords(subspace* vd) const
{
    for (int y = 0; y < m_grid.count(); ++y) {
        auto const& row = m_grid.at(y);
        for (int x = 0; x < row.count(); ++x) {
            if (row.at(x) == vd) {
                return QPoint(x, y);
            }
        }
    }

    return QPoint(-1, -1);
}

subspace* subspace_grid::at(const QPoint& coords) const
{
    if (coords.y() >= m_grid.count()) {
        return nullptr;
    }

    auto const& row = m_grid.at(coords.y());
    if (coords.x() >= row.count()) {
        return nullptr;
    }

    return row.at(coords.x());
}

int subspace_grid::width() const
{
    return m_size.width();
}

int subspace_grid::height() const
{
    return m_size.height();
}

QSize const& subspace_grid::size() const
{
    return m_size;
}

}
