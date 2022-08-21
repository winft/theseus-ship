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

#include "singleton_interface.h"

#include "base/logging.h"
#include "render/outline.h"

#include <QApplication>
#include <QDesktopWidget>

namespace KWin::scripting
{

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
    return kwinApp()->get_base().topology.size;
}

int space::displayWidth() const
{
    return displaySize().width();
}

int space::displayHeight() const
{
    return displaySize().height();
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

void space::showOutline(int x, int y, int width, int height)
{
    showOutline(QRect(x, y, width, height));
}

window* space::getClient(qulonglong windowId)
{
    return get_client_impl(windowId);
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
    return kwinApp()->get_base().get_outputs().size();
}

QRect space::virtualScreenGeometry() const
{
    return QRect({}, kwinApp()->get_base().topology.size);
}

QSize space::virtualScreenSize() const
{
    return kwinApp()->get_base().topology.size;
}

qt_script_space::qt_script_space()
{
    singleton_interface::qt_script_space = this;
}

qt_script_space::~qt_script_space()
{
    singleton_interface::qt_script_space = nullptr;
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
