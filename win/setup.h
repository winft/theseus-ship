/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_SETUP_H
#define KWIN_WIN_SETUP_H

#include "deco.h"
#include "screen.h"
#include "win.h"

#include "appmenu.h"
#include "decorations/decorationbridge.h"
#include "wayland_server.h"

#include <KDecoration2/Decoration>
#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

template<typename Win>
void setup_connections(Win* win)
{
    QObject::connect(win, &Win::geometryShapeChanged, win, &Win::geometryChanged);

    auto signalMaximizeChanged
        = static_cast<void (Win::*)(Win*, win::maximize_mode)>(&Win::clientMaximizedStateChanged);
    QObject::connect(win, signalMaximizeChanged, win, &Win::geometryChanged);

    QObject::connect(win, &Win::clientStepUserMovedResized, win, &Win::geometryChanged);
    QObject::connect(win, &Win::clientStartUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(win, &Win::clientFinishUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(
        win, &Win::clientStartUserMovedResized, win, &Win::removeCheckScreenConnection);
    QObject::connect(
        win, &Win::clientFinishUserMovedResized, win, &Win::setupCheckScreenConnection);

    QObject::connect(win, &Win::paletteChanged, win, [win] { trigger_decoration_repaint(win); });

    QObject::connect(
        Decoration::DecorationBridge::self(), &QObject::destroyed, win, &Win::destroyDecoration);

    // Replace on-screen-display on size changes
    QObject::connect(win,
                     &Win::geometryShapeChanged,
                     win,
                     [win]([[maybe_unused]] Toplevel* toplevel, QRect const& old) {
                         if (!is_on_screen_display(win)) {
                             // Not an on-screen-display.
                             return;
                         }
                         if (win->frameGeometry().isEmpty()) {
                             // No current geometry to set.
                             return;
                         }
                         if (old.size() == win->frameGeometry().size()) {
                             // No change.
                             return;
                         }
                         if (win->isInitialPositionSet()) {
                             // Position (geometry?) already set.
                             return;
                         }
                         geometry_updates_blocker blocker(win);

                         auto const area = workspace()->clientArea(
                             PlacementArea, Screens::self()->current(), win->desktop());

                         Placement::self()->place(win, area);
                         win->setGeometryRestore(win->frameGeometry());
                     });

    QObject::connect(win, &Win::paddingChanged, win, [win]() {
        win->set_visible_rect_before_geometry_update(win->visibleRect());
    });

    QObject::connect(ApplicationMenu::self(),
                     &ApplicationMenu::applicationMenuEnabledChanged,
                     win,
                     [win] { Q_EMIT win->hasApplicationMenuChanged(win->hasApplicationMenu()); });
}

template<typename Win>
void setup_wayland_plasma_management(Win* win)
{
    if (win->windowManagementInterface()) {
        // Already setup.
        return;
    }
    if (!waylandServer() || !win->surface()) {
        return;
    }
    if (!waylandServer()->windowManagement()) {
        return;
    }
    auto plasma_win
        = waylandServer()->windowManagement()->createWindow(waylandServer()->windowManagement());
    plasma_win->setTitle(win->caption());
    plasma_win->setActive(win->isActive());
    plasma_win->setFullscreen(win->isFullScreen());
    plasma_win->setKeepAbove(win->keepAbove());
    plasma_win->setKeepBelow(win->keepBelow());
    plasma_win->setMaximized(win->maximizeMode() == win::maximize_mode::full);
    plasma_win->setMinimized(win->isMinimized());
    plasma_win->setOnAllDesktops(win->isOnAllDesktops());
    plasma_win->setDemandsAttention(win->isDemandingAttention());
    plasma_win->setCloseable(win->isCloseable());
    plasma_win->setMaximizeable(win->isMaximizable());
    plasma_win->setMinimizeable(win->isMinimizable());
    plasma_win->setFullscreenable(win->isFullScreenable());
    plasma_win->setIcon(win->icon());
    auto updateAppId = [win, plasma_win] {
        plasma_win->setAppId(QString::fromUtf8(
            win->desktopFileName().isEmpty() ? win->resourceClass() : win->desktopFileName()));
    };
    updateAppId();
    plasma_win->setSkipTaskbar(win->skipTaskbar());
    plasma_win->setSkipSwitcher(win->skipSwitcher());
    plasma_win->setPid(win->pid());
    plasma_win->setShadeable(win->isShadeable());
    plasma_win->setShaded(win->isShade());
    plasma_win->setResizable(win->isResizable());
    plasma_win->setMovable(win->isMovable());

    // FIXME Matches X11Client::actionSupported(), but both should be implemented.
    plasma_win->setVirtualDesktopChangeable(true);

    plasma_win->setParentWindow(
        win->transientFor() ? win->transientFor()->windowManagementInterface() : nullptr);
    plasma_win->setGeometry(win->frameGeometry());
    QObject::connect(win, &AbstractClient::skipTaskbarChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipTaskbar(win->skipTaskbar());
    });
    QObject::connect(win, &AbstractClient::skipSwitcherChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipSwitcher(win->skipSwitcher());
    });
    QObject::connect(win, &AbstractClient::captionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setTitle(win->caption());
    });

    QObject::connect(win, &AbstractClient::activeChanged, plasma_win, [plasma_win, win] {
        plasma_win->setActive(win->isActive());
    });
    QObject::connect(win, &AbstractClient::fullScreenChanged, plasma_win, [plasma_win, win] {
        plasma_win->setFullscreen(win->isFullScreen());
    });
    QObject::connect(win,
                     &AbstractClient::keepAboveChanged,
                     plasma_win,
                     &Wrapland::Server::PlasmaWindow::setKeepAbove);
    QObject::connect(win,
                     &AbstractClient::keepBelowChanged,
                     plasma_win,
                     &Wrapland::Server::PlasmaWindow::setKeepBelow);
    QObject::connect(win, &AbstractClient::minimizedChanged, plasma_win, [plasma_win, win] {
        plasma_win->setMinimized(win->isMinimized());
    });
    QObject::connect(win,
                     static_cast<void (AbstractClient::*)(AbstractClient*, win::maximize_mode)>(
                         &AbstractClient::clientMaximizedStateChanged),
                     plasma_win,
                     [plasma_win](KWin::AbstractClient* c, win::maximize_mode mode) {
                         Q_UNUSED(c);
                         plasma_win->setMaximized(mode == win::maximize_mode::full);
                     });
    QObject::connect(win, &AbstractClient::demandsAttentionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setDemandsAttention(win->isDemandingAttention());
    });
    QObject::connect(win, &AbstractClient::iconChanged, plasma_win, [plasma_win, win] {
        plasma_win->setIcon(win->icon());
    });
    QObject::connect(win, &AbstractClient::windowClassChanged, plasma_win, updateAppId);
    QObject::connect(win, &AbstractClient::desktopFileNameChanged, plasma_win, updateAppId);
    QObject::connect(win, &AbstractClient::shadeChanged, plasma_win, [plasma_win, win] {
        plasma_win->setShaded(win->isShade());
    });
    QObject::connect(win, &AbstractClient::transientChanged, plasma_win, [plasma_win, win] {
        plasma_win->setParentWindow(
            win->transientFor() ? win->transientFor()->windowManagementInterface() : nullptr);
    });
    QObject::connect(win, &AbstractClient::geometryChanged, plasma_win, [plasma_win, win] {
        plasma_win->setGeometry(win->frameGeometry());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::closeRequested, win, [win] {
        win->closeWindow();
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::moveRequested, win, [win] {
        Cursor::setPos(win->frameGeometry().center());
        win->performMouseCommand(Options::MouseMove, Cursor::pos());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::resizeRequested, win, [win] {
        Cursor::setPos(win->frameGeometry().bottomRight());
        win->performMouseCommand(Options::MouseResize, Cursor::pos());
    });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::fullscreenRequested,
                     win,
                     [win](bool set) { win->setFullScreen(set, false); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::minimizedRequested, win, [win](bool set) {
            if (set) {
                win->minimize();
            } else {
                win->unminimize();
            }
        });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::maximizedRequested, win, [win](bool set) {
            win::maximize(win, set ? win::maximize_mode::full : win::maximize_mode::restore);
        });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepAboveRequested,
                     win,
                     [win](bool set) { win->setKeepAbove(set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepBelowRequested,
                     win,
                     [win](bool set) { win->setKeepBelow(set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::demandsAttentionRequested,
                     win,
                     [win](bool set) { win->demandAttention(set); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::activeRequested, win, [win](bool set) {
            if (set) {
                workspace()->activateClient(win, true);
            }
        });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::shadedRequested,
                     win,
                     [win](bool set) { set_shade(win, set); });

    for (auto const vd : win->desktops()) {
        plasma_win->addPlasmaVirtualDesktop(vd->id());
    }

    // Only for the legacy mechanism.
    QObject::connect(win, &AbstractClient::desktopChanged, plasma_win, [plasma_win, win] {
        if (win->isOnAllDesktops()) {
            plasma_win->setOnAllDesktops(true);
            return;
        }
        plasma_win->setOnAllDesktops(false);
    });

    // Plasma Virtual desktop management
    // show/hide when the window enters/exits from desktop
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterPlasmaVirtualDesktopRequested,
                     win,
                     [win](const QString& desktopId) {
                         VirtualDesktop* vd
                             = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             enter_desktop(win, vd);
                         }
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterNewPlasmaVirtualDesktopRequested,
                     win,
                     [win]() {
                         VirtualDesktopManager::self()->setCount(
                             VirtualDesktopManager::self()->count() + 1);
                         enter_desktop(win, VirtualDesktopManager::self()->desktops().last());
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::leavePlasmaVirtualDesktopRequested,
                     win,
                     [win](const QString& desktopId) {
                         VirtualDesktop* vd
                             = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             leave_desktop(win, vd);
                         }
                     });

    win->setWindowManagementInterface(plasma_win);
}

}

#endif
