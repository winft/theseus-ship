/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"
#include "win/meta.h"

#include <QVector>

namespace KWin::debug
{

constexpr uint32_t s_idDistance{10000};
constexpr uint32_t s_propertyBitMask{0xFFFF0000};
constexpr uint32_t s_clientBitMask{0x0000FFFF};

// TODO(romangg): Put these into the model class and its subclasses.
constexpr int s_x11ClientId{1};
constexpr int s_x11UnmanagedId{2};
constexpr int s_waylandClientId{3};
constexpr int s_workspaceInternalId{4};

template<typename Win>
QObject* get_qobject(Win* win)
{
    if (win->control && win->control->scripting) {
        return win->control->scripting.get();
    }
    return win->qobject.get();
}

template<typename Model, typename Container>
QModelIndex
index_for_window(Model const* model, int row, int column, Container const& windows, int id)
{
    if (column != 0) {
        return QModelIndex();
    }
    if (row >= windows.count()) {
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
        if (row >= get_qobject(window)->metaObject()->propertyCount()) {
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
    return get_qobject(window)->metaObject()->propertyCount();
}

template<class Window>
Window* window_for_index(QModelIndex const& index, QVector<Window*> const& windows, int id)
{
    int32_t const row = (index.internalId() & s_clientBitMask) - (s_idDistance * id);
    if (row < 0 || row >= windows.count()) {
        return nullptr;
    }
    return windows.at(row);
}

template<typename Window>
QVariant window_data(QModelIndex const& index, int role, QVector<Window*> const& windows)
{
    if (index.row() >= windows.count()) {
        return QVariant();
    }

    auto window = windows.at(index.row());
    if (role == Qt::DisplayRole) {
        return QStringLiteral("%1: %2")
            .arg(static_cast<Toplevel*>(window)->xcb_window)
            .arg(win::caption(window));
    } else if (role == Qt::DecorationRole) {
        return window->control->icon();
    }

    return QVariant();
}

template<typename Model, typename Window>
void add_window(Model* model, int parentRow, QVector<Window*>& windows, Window* window)
{
    model->begin_insert_rows(
        model->index(parentRow, 0, QModelIndex()), windows.count(), windows.count());
    windows.append(window);
    model->end_insert_rows();
}

template<typename Model, typename Window>
void remove_window(Model* model, int parentRow, QVector<Window*>& windows, Window* window)
{
    int const remove = windows.indexOf(window);
    if (remove == -1) {
        return;
    }
    model->begin_remove_rows(model->index(parentRow, 0, QModelIndex()), remove, remove);
    windows.removeAt(remove);
    model->end_remove_rows();
}

}
