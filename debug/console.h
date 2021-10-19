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

#include <config-kwin.h>
#include <kwin_export.h>

#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <QVector>

namespace Ui
{
class debug_console;
}

namespace KWin
{

namespace win
{
class InternalClient;

namespace wayland
{
class window;
}
namespace x11
{
class window;
}
}

namespace input::dbus
{
class device;
}

class X11Client;
class Toplevel;

namespace debug
{

class input_filter;

class KWIN_EXPORT console_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit console_model(QObject* parent = nullptr);
    ~console_model() override;

    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    int rowCount(const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;

private:
    template<class T>
    QModelIndex indexForClient(int row, int column, const QVector<T*>& clients, int id) const;
    template<class T>
    QModelIndex indexForProperty(int row,
                                 int column,
                                 const QModelIndex& parent,
                                 T* (console_model::*filter)(const QModelIndex&) const) const;
    template<class T>
    int propertyCount(const QModelIndex& parent,
                      T* (console_model::*filter)(const QModelIndex&) const) const;
    QVariant propertyData(QObject* object, const QModelIndex& index, int role) const;
    template<class T>
    QVariant clientData(const QModelIndex& index, int role, const QVector<T*> clients) const;
    template<class T>
    void add(int parentRow, QVector<T*>& clients, T* client);
    template<class T>
    void remove(int parentRow, QVector<T*>& clients, T* client);
    win::wayland::window* shellClient(const QModelIndex& index) const;
    win::InternalClient* internalClient(const QModelIndex& index) const;
    win::x11::window* x11Client(const QModelIndex& index) const;
    Toplevel* unmanaged(const QModelIndex& index) const;
    int topLevelRowCount() const;

    QVector<win::wayland::window*> m_shellClients;
    QVector<win::InternalClient*> m_internalClients;
    QVector<win::x11::window*> m_x11Clients;
    QVector<Toplevel*> m_unmanageds;
};

class console_delegate : public QStyledItemDelegate
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
    console();
    ~console() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initGLTab();
    void updateKeyboardTab();

    QScopedPointer<Ui::debug_console> m_ui;
    QScopedPointer<input_filter> m_inputFilter;
};

}
}
