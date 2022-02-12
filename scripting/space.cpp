/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2010 Rohan Prabhu <rohan@rohanprabhu.com>
Copyright (C) 2011, 2012 Martin Gräßlin <mgraesslin@kde.org>

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
#include "space.h"

#include "base/logging.h"
#include "render/outline.h"

#include <QApplication>
#include <QDesktopWidget>

namespace KWin::scripting
{

void space::handle_client_added(Toplevel* client)
{
    if (!client->control) {
        // Only windows with control are made available to the scripting system.
        return;
    }
    client->control->scripting = std::make_unique<window>(client, this);
    auto scr_win = client->control->scripting.get();

    setupAbstractClientConnections(scr_win);
    if (client->isClient()) {
        setupClientConnections(scr_win);
    }

    windows_count++;
    Q_EMIT clientAdded(scr_win);
}

void space::handle_client_removed(Toplevel* client)
{
    if (client->control) {
        windows_count--;
        Q_EMIT clientRemoved(client->control->scripting.get());
    }
}

window* space::get_window(Toplevel* client) const
{
    if (!client || !client->control) {
        return nullptr;
    }
    return client->control->scripting.get();
}

int space::currentDesktop() const
{
    return win::virtual_desktop_manager::self()->current();
}

int space::numberOfDesktops() const
{
    return win::virtual_desktop_manager::self()->count();
}

void space::setCurrentDesktop(int desktop)
{
    win::virtual_desktop_manager::self()->setCurrent(desktop);
}

void space::setNumberOfDesktops(int count)
{
    win::virtual_desktop_manager::self()->setCount(count);
}

QString space::currentActivity() const
{
    return {};
}

void space::setCurrentActivity(QString /*activity*/)
{
}

QStringList space::activityList() const
{
    return {};
}

QSize space::workspaceSize() const
{
    return QSize(workspaceWidth(), workspaceHeight());
}

QSize space::displaySize() const
{
    return kwinApp()->get_base().screens.displaySize();
}

int space::displayWidth() const
{
    return displaySize().width();
}

int space::displayHeight() const
{
    return displaySize().height();
}

QString space::desktopName(int desktop) const
{
    return win::virtual_desktop_manager::self()->name(desktop);
}

void space::createDesktop(int position, const QString& name) const
{
    win::virtual_desktop_manager::self()->createVirtualDesktop(position, name);
}

void space::removeDesktop(int position) const
{
    auto vd = win::virtual_desktop_manager::self()->desktopForX11Id(position + 1);
    if (!vd) {
        return;
    }

    win::virtual_desktop_manager::self()->removeVirtualDesktop(vd->id());
}

void space::setupAbstractClientConnections(window* window)
{
    connect(window, &window::clientMinimized, this, &space::clientMinimized);
    connect(window, &window::clientUnminimized, this, &space::clientUnminimized);
    connect(window, &window::clientMaximizedStateChanged, this, &space::clientMaximizeSet);
}

void space::setupClientConnections(window* window)
{
    connect(window, &window::clientFullScreenSet, this, &space::clientFullScreenSet);
}

void space::showOutline(const QRect& geometry)
{
    workspace()->outline->show(geometry);
}

void space::showOutline(int x, int y, int width, int height)
{
    workspace()->outline->show(QRect(x, y, width, height));
}

void space::hideOutline()
{
    workspace()->outline->hide();
}

window* space::getClient(qulonglong windowId)
{
    return get_client_impl(windowId);
}

QSize space::desktopGridSize() const
{
    return win::virtual_desktop_manager::self()->grid().size();
}

int space::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int space::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int space::workspaceHeight() const
{
    return desktopGridHeight() * displayHeight();
}

int space::workspaceWidth() const
{
    return desktopGridWidth() * displayWidth();
}

int space::numScreens() const
{
    return kwinApp()->get_base().screens.count();
}

int space::activeScreen() const
{
    return kwinApp()->get_base().screens.current();
}

QRect space::virtualScreenGeometry() const
{
    return kwinApp()->get_base().screens.geometry();
}

QSize space::virtualScreenSize() const
{
    return kwinApp()->get_base().screens.size();
}

QList<window*> qt_script_space::clientList() const
{
    QList<window*> ret;
    for (auto const& window : windows()) {
        ret << window;
    }
    return ret;
}

QQmlListProperty<window> declarative_script_space::clients()
{
    return QQmlListProperty<window>(this,
                                    this,
                                    &declarative_script_space::countClientList,
                                    &declarative_script_space::atClientList);
}

int declarative_script_space::countClientList(QQmlListProperty<window>* clients)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);
    return wsw->windows_count;
}

window* declarative_script_space::atClientList(QQmlListProperty<window>* clients, int index)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);

    try {
        return wsw->windows()[index];
    } catch (std::out_of_range const& ex) {
        return nullptr;
    }
}

void connect_legacy_screen_resize(space* receiver)
{
    QObject::connect(
        QApplication::desktop(), &QDesktopWidget::resized, receiver, &space::screenResized);
}

}
