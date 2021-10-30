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

#include "window.h"

#include "../outline.h"
#include "../screens.h"
#include "../virtualdesktops.h"
#include "../workspace.h"

#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <QApplication>
#include <QDesktopWidget>

namespace KWin::scripting
{

space::space(QObject* parent)
    : QObject(parent)
{
    KWin::Workspace* ws = KWin::Workspace::self();
    KWin::VirtualDesktopManager* vds = KWin::VirtualDesktopManager::self();

    connect(ws, &Workspace::desktopPresenceChanged, this, [this](Toplevel* client, int desktop) {
        auto window = get_window(client);
        Q_EMIT desktopPresenceChanged(window, desktop);
    });

    connect(ws, &Workspace::currentDesktopChanged, this, [this](int desktop, Toplevel* client) {
        auto window = get_window(client);
        Q_EMIT currentDesktopChanged(desktop, window);
    });

    connect(ws, &Workspace::clientAdded, this, &space::handle_client_added);
    connect(ws, &Workspace::clientRemoved, this, &space::handle_client_removed);
    connect(ws, &Workspace::wayland_window_added, this, &space::handle_client_added);

    connect(ws, &Workspace::clientActivated, this, [this](Toplevel* client) {
        auto window = get_window(client);
        Q_EMIT clientActivated(window);
    });

    connect(
        ws, &Workspace::clientDemandsAttentionChanged, this, [this](Toplevel* client, bool set) {
            auto window = get_window(client);
            Q_EMIT clientDemandsAttentionChanged(window, set);
        });

    connect(vds, &VirtualDesktopManager::countChanged, this, &space::numberDesktopsChanged);
    connect(vds, &VirtualDesktopManager::layoutChanged, this, &space::desktopLayoutChanged);

    connect(screens(), &Screens::sizeChanged, this, &space::virtualScreenSizeChanged);
    connect(screens(), &Screens::geometryChanged, this, &space::virtualScreenGeometryChanged);
    connect(screens(), &Screens::countChanged, this, [this](int previousCount, int currentCount) {
        Q_UNUSED(previousCount)
        emit numberScreensChanged(currentCount);
    });
    // TODO Plasma 6: Remove it.
    connect(QApplication::desktop(), &QDesktopWidget::resized, this, &space::screenResized);

    for (auto client : ws->allClientList()) {
        handle_client_added(client);
    }
}

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

window* space::activeClient() const
{
    auto active_client = workspace()->activeClient();
    if (!active_client) {
        return nullptr;
    }
    return get_window(active_client);
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

#define SLOTWRAPPER(name)                                                                          \
    void space::name()                                                                             \
    {                                                                                              \
        Workspace::self()->name();                                                                 \
    }

SLOTWRAPPER(slotSwitchToNextScreen)
SLOTWRAPPER(slotWindowToNextScreen)
SLOTWRAPPER(slotToggleShowDesktop)

SLOTWRAPPER(slotWindowMaximize)
SLOTWRAPPER(slotWindowMaximizeVertical)
SLOTWRAPPER(slotWindowMaximizeHorizontal)
SLOTWRAPPER(slotWindowMinimize)
SLOTWRAPPER(slotWindowRaise)
SLOTWRAPPER(slotWindowLower)
SLOTWRAPPER(slotWindowRaiseOrLower)
SLOTWRAPPER(slotActivateAttentionWindow)
SLOTWRAPPER(slotWindowPackLeft)
SLOTWRAPPER(slotWindowPackRight)
SLOTWRAPPER(slotWindowPackUp)
SLOTWRAPPER(slotWindowPackDown)
SLOTWRAPPER(slotWindowGrowHorizontal)
SLOTWRAPPER(slotWindowGrowVertical)
SLOTWRAPPER(slotWindowShrinkHorizontal)
SLOTWRAPPER(slotWindowShrinkVertical)

SLOTWRAPPER(slotIncreaseWindowOpacity)
SLOTWRAPPER(slotLowerWindowOpacity)

SLOTWRAPPER(slotWindowOperations)
SLOTWRAPPER(slotWindowClose)
SLOTWRAPPER(slotWindowMove)
SLOTWRAPPER(slotWindowResize)
SLOTWRAPPER(slotWindowAbove)
SLOTWRAPPER(slotWindowBelow)
SLOTWRAPPER(slotWindowOnAllDesktops)
SLOTWRAPPER(slotWindowFullScreen)
SLOTWRAPPER(slotWindowNoBorder)

SLOTWRAPPER(slotWindowToNextDesktop)
SLOTWRAPPER(slotWindowToPreviousDesktop)
SLOTWRAPPER(slotWindowToDesktopRight)
SLOTWRAPPER(slotWindowToDesktopLeft)
SLOTWRAPPER(slotWindowToDesktopUp)
SLOTWRAPPER(slotWindowToDesktopDown)

#undef SLOTWRAPPER

#define SLOTWRAPPER(name, modes)                                                                   \
    void space::name()                                                                             \
    {                                                                                              \
        Workspace::self()->quickTileWindow(modes);                                                 \
    }

SLOTWRAPPER(slotWindowQuickTileLeft, win::quicktiles::left)
SLOTWRAPPER(slotWindowQuickTileRight, win::quicktiles::right)
SLOTWRAPPER(slotWindowQuickTileTop, win::quicktiles::top)
SLOTWRAPPER(slotWindowQuickTileBottom, win::quicktiles::bottom)
SLOTWRAPPER(slotWindowQuickTileTopLeft, win::quicktiles::top | win::quicktiles::left)
SLOTWRAPPER(slotWindowQuickTileTopRight, win::quicktiles::top | win::quicktiles::right)
SLOTWRAPPER(slotWindowQuickTileBottomLeft, win::quicktiles::bottom | win::quicktiles::left)
SLOTWRAPPER(slotWindowQuickTileBottomRight, win::quicktiles::bottom | win::quicktiles::right)

#undef SLOTWRAPPER

#define SLOTWRAPPER(name, direction)                                                               \
    void space::name()                                                                             \
    {                                                                                              \
        Workspace::self()->switchWindow(Workspace::direction);                                     \
    }

SLOTWRAPPER(slotSwitchWindowUp, DirectionNorth)
SLOTWRAPPER(slotSwitchWindowDown, DirectionSouth)
SLOTWRAPPER(slotSwitchWindowRight, DirectionEast)
SLOTWRAPPER(slotSwitchWindowLeft, DirectionWest)

#undef SLOTWRAPPER

#define SLOTWRAPPER(name, direction)                                                               \
    void space::name()                                                                             \
    {                                                                                              \
        VirtualDesktopManager::self()->moveTo<direction>(options->isRollOverDesktops());           \
    }

SLOTWRAPPER(slotSwitchDesktopNext, DesktopNext)
SLOTWRAPPER(slotSwitchDesktopPrevious, DesktopPrevious)
SLOTWRAPPER(slotSwitchDesktopRight, DesktopRight)
SLOTWRAPPER(slotSwitchDesktopLeft, DesktopLeft)
SLOTWRAPPER(slotSwitchDesktopUp, DesktopAbove)
SLOTWRAPPER(slotSwitchDesktopDown, DesktopBelow)

#undef SLOTWRAPPER

void space::setActiveClient(window* window)
{
    KWin::Workspace::self()->activateClient(window->client());
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

QRect space::clientArea(ClientAreaOption option, const QPoint& p, int desktop) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), p, desktop);
}

QRect space::clientArea(ClientAreaOption option, window* window) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), window->client());
}

QRect space::clientArea(ClientAreaOption option, window const* c) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), c->client());
}

QRect space::clientArea(ClientAreaOption option, int screen, int desktop) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), screen, desktop);
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

QString space::supportInformation() const
{
    return Workspace::self()->supportInformation();
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

void space::sendClientToScreen(window* client, int screen)
{
    if (screen < 0 || screen >= screens()->count()) {
        return;
    }
    workspace()->sendClientToScreen(client->client(), screen);
}

qt_script_space::qt_script_space(QObject* parent)
    : space(parent)
{
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

declarative_script_space::declarative_script_space(QObject* parent)
    : space(parent)
{
}

} // KWin
