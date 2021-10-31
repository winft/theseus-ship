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

#include "../outline.h"

namespace KWin::scripting
{

void space::handle_client_added(Toplevel* client)
{
    if (!client->control) {
        // Only windows with control are made available to the scripting system.
        return;
    }
    auto wrapper = std::make_unique<window>(client, this);

    setupAbstractClientConnections(wrapper.get());
    if (client->isClient()) {
        setupClientConnections(wrapper.get());
    }

    Q_EMIT clientAdded(wrapper.get());
    m_windows.push_back(std::move(wrapper));
}

void space::handle_client_removed(Toplevel* client)
{
    auto remover = [this, client](auto& wrapper) {
        if (wrapper->client() == client) {
            Q_EMIT clientRemoved(wrapper.get());
            return true;
        }
        return false;
    };
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(), remover), m_windows.end());
}

window* space::get_window(Toplevel* client) const
{
    auto const it
        = std::find_if(m_windows.cbegin(), m_windows.cend(), [client](auto const& window) {
              return window->client() == client;
          });
    return it != m_windows.cend() ? it->get() : nullptr;
}

int space::currentDesktop() const
{
    return VirtualDesktopManager::self()->current();
}

int space::numberOfDesktops() const
{
    return VirtualDesktopManager::self()->count();
}

void space::setCurrentDesktop(int desktop)
{
    VirtualDesktopManager::self()->setCurrent(desktop);
}

void space::setNumberOfDesktops(int count)
{
    VirtualDesktopManager::self()->setCount(count);
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
    return screens()->displaySize();
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
    return VirtualDesktopManager::self()->name(desktop);
}

void space::createDesktop(int position, const QString& name) const
{
    VirtualDesktopManager::self()->createVirtualDesktop(position, name);
}

void space::removeDesktop(int position) const
{
    VirtualDesktop* vd = VirtualDesktopManager::self()->desktopForX11Id(position + 1);
    if (!vd) {
        return;
    }

    VirtualDesktopManager::self()->removeVirtualDesktop(vd->id());
}

void space::setupAbstractClientConnections(window* window)
{
    connect(window, &window::clientMinimized, this, &space::clientMinimized);
    connect(window, &window::clientUnminimized, this, &space::clientUnminimized);
    connect(window, &window::clientMaximizedStateChanged, this, &space::clientMaximizeSet);
}

void space::setupClientConnections(window* window)
{
    connect(window, &window::clientManaging, this, &space::clientManaging);
    connect(window, &window::clientFullScreenSet, this, &space::clientFullScreenSet);
}

void space::showOutline(const QRect& geometry)
{
    outline()->show(geometry);
}

void space::showOutline(int x, int y, int width, int height)
{
    outline()->show(QRect(x, y, width, height));
}

void space::hideOutline()
{
    outline()->hide();
}

window* space::getClient(qulonglong windowId)
{
    auto const it
        = std::find_if(m_windows.cbegin(), m_windows.cend(), [windowId](auto const& client) {
              return client->windowId() == windowId;
          });
    return it != m_windows.cend() ? it->get() : nullptr;
}

QSize space::desktopGridSize() const
{
    return VirtualDesktopManager::self()->grid().size();
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
    return screens()->count();
}

int space::activeScreen() const
{
    return screens()->current();
}

QRect space::virtualScreenGeometry() const
{
    return screens()->geometry();
}

QSize space::virtualScreenSize() const
{
    return screens()->size();
}

std::vector<window*> space::windows() const
{
    std::vector<window*> ret;
    for (auto const& client : m_windows) {
        ret.push_back(client.get());
    }
    return ret;
}

QList<window*> qt_script_space::clientList() const
{
    QList<window*> ret;
    for (auto const& client : m_windows) {
        ret << client.get();
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
    return wsw->m_windows.size();
}

window* declarative_script_space::atClientList(QQmlListProperty<window>* clients, int index)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);

    try {
        return wsw->m_windows[index].get();
    } catch (std::out_of_range const& ex) {
        return nullptr;
    }
}

} // KWin
