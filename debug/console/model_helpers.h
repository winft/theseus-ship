/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "toplevel.h"
#include "win/meta.h"

#include <QVector>

namespace KWin::debug
{

constexpr uint32_t s_idDistance{10000};
constexpr uint32_t s_propertyBitMask{0xFFFF0000};
constexpr uint32_t s_clientBitMask{0x0000FFFF};

template<typename Model, typename Container>
QModelIndex
index_for_window(Model const* model, int row, int column, Container const& windows, int id)
{
    if (column != 0) {
        return QModelIndex();
    }
    if (row >= static_cast<int>(windows.size())) {
        return QModelIndex();
    }
    return model->create_index(row, column, s_idDistance * id + row);
}

template<typename Model, typename Window>
QModelIndex index_for_property(Model const* model,
                               int row,
                               int column,
                               const QModelIndex& parent,
                               Window* (Model::*filter)(QModelIndex const&) const)
{
    if (auto window = (model->*filter)(parent)) {
        if (row >= window->metaObject()->propertyCount()) {
            return QModelIndex();
        }
        return model->create_index(row, column, quint32(row + 1) << 16 | parent.internalId());
    }
    return QModelIndex();
}

template<typename Model, typename Window>
int window_property_count(Model const* model,
                          QModelIndex const& parent,
                          Window* (Model::*filter)(QModelIndex const&) const)
{
    auto window = (model->*filter)(parent);
    if (!window) {
        return 0;
    }
    return window->metaObject()->propertyCount();
}

inline console_window* window_for_index(QModelIndex const& index,
                                        std::vector<std::unique_ptr<console_window>> const& windows,
                                        int id)
{
    int32_t const row = (index.internalId() & s_clientBitMask) - (s_idDistance * id);
    if (row < 0 || row >= static_cast<int>(windows.size())) {
        return nullptr;
    }
    return windows.at(row).get();
}

inline QVariant window_data(QModelIndex const& index,
                            int role,
                            std::vector<std::unique_ptr<console_window>> const& windows)
{
    if (index.row() >= static_cast<int>(windows.size())) {
        return QVariant();
    }

    auto& window = windows.at(index.row());
    if (role == Qt::DisplayRole) {
        return QStringLiteral("%1: %2").arg(window->windowId()).arg(window->caption());
    } else if (role == Qt::DecorationRole) {
        return window->icon();
    }

    return QVariant();
}

template<typename Model, typename Window>
void add_window(Model* model,
                int parentRow,
                std::vector<std::unique_ptr<console_window>>& windows,
                Window* window)
{
    model->begin_insert_rows(
        model->index(parentRow, 0, QModelIndex()), windows.size(), windows.size());
    windows.emplace_back(std::make_unique<console_window>(window));
    model->end_insert_rows();
}

template<typename Model, typename Window>
void remove_window(Model* model,
                   int parentRow,
                   std::vector<std::unique_ptr<console_window>>& windows,
                   Window* window)
{
    auto it = std::find_if(
        windows.begin(), windows.end(), [window](auto& win) { return win->ref_win == window; });

    if (it == windows.end()) {
        return;
    }

    int const remove = it - windows.begin();

    model->begin_remove_rows(model->index(parentRow, 0, QModelIndex()), remove, remove);
    windows.erase(it);
    model->end_remove_rows();
}

}
