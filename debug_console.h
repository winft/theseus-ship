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
#ifndef KWIN_DEBUG_CONSOLE_H
#define KWIN_DEBUG_CONSOLE_H

#include <config-kwin.h>
#include <input/event.h>
#include <input/event_spy.h>
#include <kwin_export.h>

#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <QVector>

class QTextEdit;

namespace Ui
{
class DebugConsole;
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

class X11Client;
class Toplevel;
class DebugConsoleFilter;

class KWIN_EXPORT DebugConsoleModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit DebugConsoleModel(QObject *parent = nullptr);
    ~DebugConsoleModel() override;


    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex & parent) const override;
    int rowCount(const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;

private:
    template <class T>
    QModelIndex indexForClient(int row, int column, const QVector<T*> &clients, int id) const;
    template <class T>
    QModelIndex indexForProperty(int row, int column, const QModelIndex &parent, T *(DebugConsoleModel::*filter)(const QModelIndex&) const) const;
    template <class T>
    int propertyCount(const QModelIndex &parent, T *(DebugConsoleModel::*filter)(const QModelIndex&) const) const;
    QVariant propertyData(QObject *object, const QModelIndex &index, int role) const;
    template <class T>
    QVariant clientData(const QModelIndex &index, int role, const QVector<T*> clients) const;
    template <class T>
    void add(int parentRow, QVector<T*> &clients, T *client);
    template <class T>
    void remove(int parentRow, QVector<T*> &clients, T *client);
    win::wayland::window* shellClient(const QModelIndex &index) const;
    win::InternalClient *internalClient(const QModelIndex &index) const;
    win::x11::window* x11Client(const QModelIndex &index) const;
    Toplevel* unmanaged(const QModelIndex &index) const;
    int topLevelRowCount() const;

    QVector<win::wayland::window*> m_shellClients;
    QVector<win::InternalClient*> m_internalClients;
    QVector<win::x11::window*> m_x11Clients;
    QVector<Toplevel*> m_unmanageds;

};

class DebugConsoleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit DebugConsoleDelegate(QObject *parent = nullptr);
    ~DebugConsoleDelegate() override;

    QString displayText(const QVariant &value, const QLocale &locale) const override;
};

class KWIN_EXPORT DebugConsole : public QWidget
{
    Q_OBJECT
public:
    DebugConsole();
    ~DebugConsole() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void initGLTab();
    void updateKeyboardTab();

    QScopedPointer<Ui::DebugConsole> m_ui;
    QScopedPointer<DebugConsoleFilter> m_inputFilter;
};

class SurfaceTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit SurfaceTreeModel(QObject *parent = nullptr);
    ~SurfaceTreeModel() override;

    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex & parent) const override;
    int rowCount(const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
};

class DebugConsoleFilter : public input::event_spy
{
public:
    explicit DebugConsoleFilter(QTextEdit *textEdit);
    ~DebugConsoleFilter() override;

    void button(input::button_event const& event) override;
    void motion(input::motion_event const& event) override;
    void axis(input::axis_event const& event) override;

    void key(input::key_event const& event) override;
    void key_repeat(input::key_event const& event) override;

    void touchDown(qint32 id, const QPointF &pos, quint32 time) override;
    void touchMotion(qint32 id, const QPointF &pos, quint32 time) override;
    void touchUp(qint32 id, quint32 time) override;

    void pinch_begin(input::pinch_begin_event const& event) override;
    void pinch_update(input::pinch_update_event const& event) override;
    void pinchGestureEnd(quint32 time) override;
    void pinchGestureCancelled(quint32 time) override;

    void swipeGestureBegin(int fingerCount, quint32 time) override;
    void swipeGestureUpdate(const QSizeF &delta, quint32 time) override;
    void swipeGestureEnd(quint32 time) override;
    void swipeGestureCancelled(quint32 time) override;

    void switchEvent(input::SwitchEvent *event) override;

    void tabletToolEvent(QTabletEvent *event) override;
    void tabletToolButtonEvent(const QSet<uint> &pressedButtons) override;
    void tabletPadButtonEvent(const QSet<uint> &pressedButtons) override;
    void tabletPadStripEvent(int number, int position, bool isFinger) override;
    void tabletPadRingEvent(int number, int position, bool isFinger) override;

private:
    QTextEdit *m_textEdit;
};

namespace input
{
namespace dbus
{
class device;
}
}

class InputDeviceModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit InputDeviceModel(QObject *parent = nullptr);
    ~InputDeviceModel() override;

    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex & parent) const override;
    int rowCount(const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;

private:
    void setupDeviceConnections(input::dbus::device* device);
    QVector<input::dbus::device*> m_devices;
};

}

#endif
