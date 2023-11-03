/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace_grid.h"

namespace KWin::win
{

subspace_grid::subspace_grid()
    : m_size(1, 2) // Default to two rows
    , m_grid{std::vector<subspace*>{}, std::vector<subspace*>{}}
{
}

subspace_grid::~subspace_grid() = default;

void subspace_grid::update(QSize const& size,
                           Qt::Orientation orientation,
                           std::vector<subspace*> const& subs)
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
            std::vector<subspace*> row;
            for (uint x = 0; x < width && it != end; ++x) {
                row.push_back(*it);
                it++;
            }
            m_grid.push_back(row);
        }
    } else {
        for (uint y = 0; y < height; ++y) {
            m_grid.push_back(std::vector<subspace*>());
        }
        for (uint x = 0; x < width; ++x) {
            for (uint y = 0; y < height && it != end; ++y) {
                auto& row = m_grid[y];
                row.push_back(*it);
                it++;
            }
        }
    }
}

QPoint subspace_grid::gridCoords(subspace* vd) const
{
    for (size_t y = 0; y < m_grid.size(); ++y) {
        auto const& row = m_grid.at(y);
        for (size_t x = 0; x < row.size(); ++x) {
            if (row.at(x) == vd) {
                return QPoint(x, y);
            }
        }
    }

    return QPoint(-1, -1);
}

subspace* subspace_grid::at(const QPoint& coords) const
{
    if (coords.y() >= static_cast<int>(m_grid.size())) {
        return nullptr;
    }

    auto const& row = m_grid.at(coords.y());
    if (coords.x() >= static_cast<int>(row.size())) {
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
