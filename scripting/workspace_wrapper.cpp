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

#include "workspace_wrapper.h"

#include "window_wrapper.h"

#include "../outline.h"
#include "../screens.h"
#include "../virtualdesktops.h"
#include "../wayland_server.h"
#include "../workspace.h"
#ifdef KWIN_BUILD_ACTIVITIES
#include "../activities.h"
#endif

#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <QDesktopWidget>
#include <QApplication>

namespace KWin {

WorkspaceWrapper::WorkspaceWrapper(QObject* parent) : QObject(parent)
{
    KWin::Workspace *ws = KWin::Workspace::self();
    KWin::VirtualDesktopManager *vds = KWin::VirtualDesktopManager::self();

    connect(ws, &Workspace::desktopPresenceChanged, this,
            [this](Toplevel* client, int desktop) {
                auto window = get_window(client);
                Q_EMIT desktopPresenceChanged(window, desktop);
            }
    );

    connect(ws, &Workspace::currentDesktopChanged, this,
            [this](int desktop, Toplevel* client) {
                auto window = get_window(client);
                Q_EMIT currentDesktopChanged(desktop, window);
            }
    );

    connect(ws, &Workspace::clientAdded, this, &WorkspaceWrapper::handle_client_added);
    connect(ws, &Workspace::clientRemoved, this, &WorkspaceWrapper::handle_client_removed);

    connect(ws, &Workspace::clientActivated, this,
        [this](Toplevel* client) {
            auto window = get_window(client);
            Q_EMIT clientActivated(window);
        }
    );

    connect(ws, &Workspace::clientDemandsAttentionChanged, this,
            [this](Toplevel* client, bool set) {
                auto window = get_window(client);
                Q_EMIT clientDemandsAttentionChanged(window, set);
            }
        );

    connect(vds, &VirtualDesktopManager::countChanged, this, &WorkspaceWrapper::numberDesktopsChanged);
    connect(vds, &VirtualDesktopManager::layoutChanged, this, &WorkspaceWrapper::desktopLayoutChanged);

#ifdef KWIN_BUILD_ACTIVITIES
    if (KWin::Activities *activities = KWin::Activities::self()) {
        connect(activities, &Activities::currentChanged, this, &WorkspaceWrapper::currentActivityChanged);
        connect(activities, &Activities::added, this, &WorkspaceWrapper::activitiesChanged);
        connect(activities, &Activities::added, this, &WorkspaceWrapper::activityAdded);
        connect(activities, &Activities::removed, this, &WorkspaceWrapper::activitiesChanged);
        connect(activities, &Activities::removed, this, &WorkspaceWrapper::activityRemoved);
    }
#endif
    connect(screens(), &Screens::sizeChanged, this, &WorkspaceWrapper::virtualScreenSizeChanged);
    connect(screens(), &Screens::geometryChanged, this, &WorkspaceWrapper::virtualScreenGeometryChanged);
    connect(screens(), &Screens::countChanged, this,
        [this] (int previousCount, int currentCount) {
            Q_UNUSED(previousCount)
            emit numberScreensChanged(currentCount);
        }
    );
    // TODO Plasma 6: Remove it.
    connect(QApplication::desktop(), &QDesktopWidget::resized, this, &WorkspaceWrapper::screenResized);
    if (waylandServer()) {
        connect(waylandServer(), &WaylandServer::window_added, this, &WorkspaceWrapper::handle_client_added);
    }

    for (auto client : ws->allClientList()) {
        handle_client_added(client);
    }
}

void WorkspaceWrapper::handle_client_added(Toplevel* client)
{
    if (!client->control) {
        // Only windows with control are made available to the scripting system.
        return;
    }
    auto wrapper = std::make_unique<WindowWrapper>(client, this);

    setupAbstractClientConnections(wrapper.get());
    if (client->isClient()) {
        setupClientConnections(wrapper.get());
    }

    Q_EMIT clientAdded(wrapper.get());
    m_windows.push_back(std::move(wrapper));
}

void WorkspaceWrapper::handle_client_removed(Toplevel* client)
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

WindowWrapper* WorkspaceWrapper::get_window(Toplevel* client) const
{
    auto const it = std::find_if(m_windows.cbegin(), m_windows.cend(),
        [client](auto const& window) {
            return window->client() == client;
        }
    );
    return it != m_windows.cend() ? it->get() : nullptr;
}

int WorkspaceWrapper::currentDesktop() const
{
    return VirtualDesktopManager::self()->current();
}

int WorkspaceWrapper::numberOfDesktops() const
{
    return VirtualDesktopManager::self()->count();
}

void WorkspaceWrapper::setCurrentDesktop(int desktop)
{
    VirtualDesktopManager::self()->setCurrent(desktop);
}

void WorkspaceWrapper::setNumberOfDesktops(int count)
{
    VirtualDesktopManager::self()->setCount(count);
}

KWin::WindowWrapper* WorkspaceWrapper::activeClient() const
{
    auto active_client = workspace()->activeClient();
    if (!active_client) {
        return nullptr;
    }
    return get_window(active_client);
}

QString WorkspaceWrapper::currentActivity() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return QString();
    }
    return Activities::self()->current();
#else
    return QString();
#endif
}

void WorkspaceWrapper::setCurrentActivity(QString activity)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (Activities::self()) {
        Activities::self()->setCurrent(activity);
    }
#else
    Q_UNUSED(activity)
#endif
}

QStringList WorkspaceWrapper::activityList() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return QStringList();
    }
    return Activities::self()->all();
#else
    return QStringList();
#endif
}

#define SLOTWRAPPER(name) \
void WorkspaceWrapper::name( ) { \
    Workspace::self()->name(); \
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

#define SLOTWRAPPER(name,modes) \
void WorkspaceWrapper::name() { \
    Workspace::self()->quickTileWindow(modes); \
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

#define SLOTWRAPPER(name,direction) \
void WorkspaceWrapper::name() { \
    Workspace::self()->switchWindow(Workspace::direction); \
}

SLOTWRAPPER(slotSwitchWindowUp, DirectionNorth)
SLOTWRAPPER(slotSwitchWindowDown, DirectionSouth)
SLOTWRAPPER(slotSwitchWindowRight, DirectionEast)
SLOTWRAPPER(slotSwitchWindowLeft, DirectionWest)

#undef SLOTWRAPPER

#define SLOTWRAPPER(name,direction) \
void WorkspaceWrapper::name( ) { \
    VirtualDesktopManager::self()->moveTo<direction>(options->isRollOverDesktops()); \
}

SLOTWRAPPER(slotSwitchDesktopNext,DesktopNext)
SLOTWRAPPER(slotSwitchDesktopPrevious,DesktopPrevious)
SLOTWRAPPER(slotSwitchDesktopRight,DesktopRight)
SLOTWRAPPER(slotSwitchDesktopLeft,DesktopLeft)
SLOTWRAPPER(slotSwitchDesktopUp,DesktopAbove)
SLOTWRAPPER(slotSwitchDesktopDown,DesktopBelow)

#undef SLOTWRAPPER

void WorkspaceWrapper::setActiveClient(KWin::WindowWrapper* window)
{
    KWin::Workspace::self()->activateClient(window->client());
}

QSize WorkspaceWrapper::workspaceSize() const
{
    return QSize(workspaceWidth(), workspaceHeight());
}

QSize WorkspaceWrapper::displaySize() const
{
    return screens()->displaySize();
}

int WorkspaceWrapper::displayWidth() const
{
    return displaySize().width();
}

int WorkspaceWrapper::displayHeight() const
{
    return displaySize().height();
}

QRect WorkspaceWrapper::clientArea(ClientAreaOption option, const QPoint &p, int desktop) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), p, desktop);
}

QRect WorkspaceWrapper::clientArea(ClientAreaOption option, KWin::WindowWrapper* window) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), window->client());
}

QRect WorkspaceWrapper::clientArea(ClientAreaOption option, KWin::WindowWrapper const* c) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), c->client());
}

QRect WorkspaceWrapper::clientArea(ClientAreaOption option, int screen, int desktop) const
{
    return Workspace::self()->clientArea(static_cast<clientAreaOption>(option), screen, desktop);
}

QString WorkspaceWrapper::desktopName(int desktop) const
{
    return VirtualDesktopManager::self()->name(desktop);
}

void WorkspaceWrapper::createDesktop(int position, const QString &name) const
{
    VirtualDesktopManager::self()->createVirtualDesktop(position, name);
}

void WorkspaceWrapper::removeDesktop(int position) const
{
    VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForX11Id(position + 1);
    if (!vd) {
        return;
    }

    VirtualDesktopManager::self()->removeVirtualDesktop(vd->id());
}

QString WorkspaceWrapper::supportInformation() const
{
    return Workspace::self()->supportInformation();
}

void WorkspaceWrapper::setupAbstractClientConnections(WindowWrapper* window)
{
    connect(window, &WindowWrapper::clientMinimized, this, &WorkspaceWrapper::clientMinimized);
    connect(window, &WindowWrapper::clientUnminimized, this, &WorkspaceWrapper::clientUnminimized);
    connect(window, &WindowWrapper::clientMaximizedStateChanged,
            this, &WorkspaceWrapper::clientMaximizeSet);
}

void WorkspaceWrapper::setupClientConnections(WindowWrapper* window)
{
    connect(window, &WindowWrapper::clientManaging, this, &WorkspaceWrapper::clientManaging);
    connect(window, &WindowWrapper::clientFullScreenSet, this, &WorkspaceWrapper::clientFullScreenSet);
}

void WorkspaceWrapper::showOutline(const QRect &geometry)
{
    outline()->show(geometry);
}

void WorkspaceWrapper::showOutline(int x, int y, int width, int height)
{
    outline()->show(QRect(x, y, width, height));
}

void WorkspaceWrapper::hideOutline()
{
    outline()->hide();
}

KWin::WindowWrapper* WorkspaceWrapper::getClient(qulonglong windowId)
{
    auto const it = std::find_if(m_windows.cbegin(), m_windows.cend(),
        [windowId](auto const& client) {
            return client->windowId() == windowId;
        }
    );
    return it != m_windows.cend() ? it->get() : nullptr;
}

QSize WorkspaceWrapper::desktopGridSize() const
{
    return VirtualDesktopManager::self()->grid().size();
}

int WorkspaceWrapper::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int WorkspaceWrapper::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int WorkspaceWrapper::workspaceHeight() const
{
    return desktopGridHeight() * displayHeight();
}

int WorkspaceWrapper::workspaceWidth() const
{
    return desktopGridWidth() * displayWidth();
}

int WorkspaceWrapper::numScreens() const
{
    return screens()->count();
}

int WorkspaceWrapper::activeScreen() const
{
    return screens()->current();
}

QRect WorkspaceWrapper::virtualScreenGeometry() const
{
    return screens()->geometry();
}

QSize WorkspaceWrapper::virtualScreenSize() const
{
    return screens()->size();
}

std::vector<WindowWrapper*> WorkspaceWrapper::windows() const
{
    std::vector<WindowWrapper*> ret;
    for (auto const& client : m_windows) {
        ret.push_back(client.get());
    }
    return ret;
}

void WorkspaceWrapper::sendClientToScreen(KWin::WindowWrapper *client, int screen)
{
    if (screen < 0 || screen >= screens()->count()) {
        return;
    }
    workspace()->sendClientToScreen(client->client(), screen);
}

QtScriptWorkspaceWrapper::QtScriptWorkspaceWrapper(QObject* parent)
    : WorkspaceWrapper(parent) {}

QList<KWin::WindowWrapper*> QtScriptWorkspaceWrapper::clientList() const
{
    QList<KWin::WindowWrapper*> ret;
    for (auto const& client : m_windows) {
        ret << client.get();
    }
    return ret;
}

QQmlListProperty<KWin::WindowWrapper> DeclarativeScriptWorkspaceWrapper::clients()
{
    return QQmlListProperty<KWin::WindowWrapper>(this,
                                                 this,
                                                 &DeclarativeScriptWorkspaceWrapper::countClientList,
                                                 &DeclarativeScriptWorkspaceWrapper::atClientList);
}

int DeclarativeScriptWorkspaceWrapper::countClientList(QQmlListProperty<KWin::WindowWrapper> *clients)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<DeclarativeScriptWorkspaceWrapper*>(clients->data);
    return wsw->m_windows.size();
}

KWin::WindowWrapper *DeclarativeScriptWorkspaceWrapper::atClientList(QQmlListProperty<KWin::WindowWrapper> *clients,
                                                                     int index)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<DeclarativeScriptWorkspaceWrapper*>(clients->data);

    try {
        return wsw->m_windows[index].get();
    }
    catch (std::out_of_range const& ex) {
        return nullptr;
    }
}

DeclarativeScriptWorkspaceWrapper::DeclarativeScriptWorkspaceWrapper(QObject* parent)
    : WorkspaceWrapper(parent) {}

} // KWin
