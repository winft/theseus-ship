/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QAbstractItemModel>

namespace KWin
{

namespace win
{
class space;
}

namespace debug
{

class surface_tree_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit surface_tree_model(win::space& space, QObject* parent = nullptr);

    int columnCount(QModelIndex const& parent) const override;
    QVariant data(QModelIndex const& index, int role) const override;
    QModelIndex index(int row, int column, QModelIndex const& parent) const override;
    int rowCount(QModelIndex const& parent) const override;
    QModelIndex parent(QModelIndex const& child) const override;

    win::space& space;
};

}
}
