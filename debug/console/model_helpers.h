/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "win/meta.h"
#include "win/space_qobject.h"
#include "win/x11/stacking.h"

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

inline win::property_window*
window_for_index(QModelIndex const& index,
                 std::vector<std::unique_ptr<win::property_window>> const& windows,
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
                            std::vector<std::unique_ptr<win::property_window>> const& windows)
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
                std::vector<std::unique_ptr<win::property_window>>& windows,
                Window* window)
{
    model->begin_insert_rows(
        model->index(parentRow, 0, QModelIndex()), windows.size(), windows.size());
    windows.emplace_back(std::make_unique<console_window<Window>>(window));
    model->end_insert_rows();
}

template<typename Model, typename Window>
void remove_window(Model* model,
                   int parentRow,
                   std::vector<std::unique_ptr<win::property_window>>& windows,
                   Window* window)
{
    auto it = std::find_if(windows.begin(), windows.end(), [window](auto& win) {
        return win->internalId() == window->meta.internal_id;
    });

    if (it == windows.end()) {
        return;
    }

    int const remove = it - windows.begin();

    model->begin_remove_rows(model->index(parentRow, 0, QModelIndex()), remove, remove);
    windows.erase(it);
    model->end_remove_rows();
}

template<typename Model, typename Space>
void model_setup_connections(Model& model, Space& space)
{
    using x11_window_t = typename Space::x11_window;

    for (auto const& win : space.windows) {
        std::visit(overload{[&](x11_window_t* win) {
                                if (win->control) {
                                    model.m_x11Clients.emplace_back(
                                        std::make_unique<console_window<x11_window_t>>(win));
                                }
                            },
                            [](auto&&) {}},
                   win);
    }
    QObject::connect(
        space.qobject.get(), &win::space_qobject::clientAdded, &model, [&](auto win_id) {
            auto c = std::get<x11_window_t*>(space.windows_map.at(win_id));
            add_window(&model, model.s_x11ClientId - 1, model.m_x11Clients, c);
        });
    QObject::connect(
        space.qobject.get(), &win::space_qobject::clientRemoved, &model, [&](auto win_id) {
            // TODO(romangg): This function is also being called on Waylad windows for
            // some reason. It works with our containers but best would be to make this
            // symmetric with adding.
            auto& win = space.windows_map.at(win_id);
            if (std::holds_alternative<x11_window_t*>(win)) {
                auto x11_win = std::get<x11_window_t*>(win);
                remove_window(&model, model.s_x11ClientId - 1, model.m_x11Clients, x11_win);
            }
        });

    for (auto unmanaged : win::x11::get_unmanageds(space)) {
        model.m_unmanageds.emplace_back(
            std::make_unique<console_window<x11_window_t>>(std::get<x11_window_t*>(unmanaged)));
    }

    QObject::connect(
        space.qobject.get(), &win::space_qobject::unmanagedAdded, &model, [&](auto win_id) {
            auto u = std::get<x11_window_t*>(space.windows_map.at(win_id));
            add_window(&model, model.s_x11UnmanagedId - 1, model.m_unmanageds, u);
        });
    QObject::connect(
        space.qobject.get(), &win::space_qobject::unmanagedRemoved, &model, [&](auto win_id) {
            auto u = std::get<x11_window_t*>(space.windows_map.at(win_id));
            remove_window(&model, model.s_x11UnmanagedId - 1, model.m_unmanageds, u);
        });
}

}
