/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "kwin_export.h"

#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <memory>
#include <vector>

namespace Ui
{
class debug_console;
}

namespace KWin
{

namespace win
{

class space;

}

namespace render
{
class scene;
}

namespace debug
{

class console_window;

class KWIN_EXPORT console_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit console_model(win::space& space, QObject* parent = nullptr);
    ~console_model() override;

    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    int rowCount(const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;

    // Wrap QAbstractItemModel functions for public consumption from free functions.
    QModelIndex create_index(int row, int column, quintptr id) const;
    void begin_insert_rows(QModelIndex const& parent, int first, int last);
    void end_insert_rows();
    void begin_remove_rows(QModelIndex const& parent, int first, int last);
    void end_remove_rows();

    virtual bool get_client_count(int parent_id, int& count) const;
    virtual bool get_property_count(QModelIndex const& parent, int& count) const;

    virtual bool get_client_index(int row, int column, int parent_id, QModelIndex& index) const;
    virtual bool
    get_property_index(int row, int column, QModelIndex const& parent, QModelIndex& index) const;

    virtual QVariant get_client_data(QModelIndex const& index, int role) const;
    virtual QVariant get_client_property_data(QModelIndex const& index, int role) const;

    QVariant propertyData(QObject* object, const QModelIndex& index, int role) const;

    console_window* internalClient(QModelIndex const& index) const;
    console_window* x11Client(QModelIndex const& index) const;
    console_window* unmanaged(QModelIndex const& index) const;
    virtual int topLevelRowCount() const;

    static constexpr int s_x11ClientId{1};
    static constexpr int s_x11UnmanagedId{2};
    static constexpr int s_waylandClientId{3};
    static constexpr int s_workspaceInternalId{4};

    std::vector<std::unique_ptr<console_window>> m_internalClients;
    std::vector<std::unique_ptr<console_window>> m_x11Clients;
    std::vector<std::unique_ptr<console_window>> m_unmanageds;
    win::space& space;
};

class KWIN_EXPORT console_delegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit console_delegate(QObject* parent = nullptr);
    ~console_delegate() override;

    QString displayText(const QVariant& value, const QLocale& locale) const override;
};

class KWIN_EXPORT console : public QWidget
{
    Q_OBJECT
public:
    console(win::space& space);
    ~console();

protected:
    void showEvent(QShowEvent* event) override;
    void initGLTab(render::scene& scene);

    QScopedPointer<Ui::debug_console> m_ui;
    win::space& space;
};

}
}
