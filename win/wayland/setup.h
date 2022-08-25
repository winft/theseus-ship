/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/actions.h"
#include "win/activation.h"
#include "win/control.h"
#include "win/meta.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/types.h"
#include "win/virtual_desktops.h"

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win::wayland
{

template<typename Space, typename Win>
void setup_plasma_management(Space* space, Win* win)
{
    if (win->control->plasma_wayland_integration) {
        // Already setup.
        return;
    }
    if (!win->surface) {
        return;
    }
    auto plasma_win
        = space->plasma_window_manager->createWindow(space->plasma_window_manager.get());
    plasma_win->setTitle(win::caption(win));
    plasma_win->setActive(win->control->active());
    plasma_win->setFullscreen(win->control->fullscreen());
    plasma_win->setKeepAbove(win->control->keep_above());
    plasma_win->setKeepBelow(win->control->keep_below());
    plasma_win->setMaximized(win->maximizeMode() == win::maximize_mode::full);
    plasma_win->setMinimized(win->control->minimized());
    plasma_win->setOnAllDesktops(win->isOnAllDesktops());
    plasma_win->setDemandsAttention(win->control->demands_attention());
    plasma_win->setCloseable(win->isCloseable());
    plasma_win->setMaximizeable(win->isMaximizable());
    plasma_win->setMinimizeable(win->isMinimizable());
    plasma_win->setFullscreenable(win->control->can_fullscreen());
    plasma_win->setIcon(win->control->icon());
    auto updateAppId = [win, plasma_win] {
        auto const name = win->control->desktop_file_name();
        plasma_win->setAppId(QString::fromUtf8(name.isEmpty() ? win->resource_class : name));
    };
    updateAppId();
    plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    plasma_win->setSkipSwitcher(win->control->skip_switcher());
    plasma_win->setPid(win->pid());
    plasma_win->setResizable(win->isResizable());
    plasma_win->setMovable(win->isMovable());
    auto const appmenu = win->control->application_menu();
    plasma_win->setApplicationMenuPaths(QString::fromStdString(appmenu.address.name),
                                        QString::fromStdString(appmenu.address.path));

    // FIXME Matches X11Client::actionSupported(), but both should be implemented.
    plasma_win->setVirtualDesktopChangeable(true);

    auto transient_lead = win->transient()->lead();
    plasma_win->setParentWindow(transient_lead ? transient_lead->control->plasma_wayland_integration
                                               : nullptr);
    plasma_win->setGeometry(win->frameGeometry());
    QObject::connect(win, &Win::skipTaskbarChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    });
    QObject::connect(win, &Win::skipSwitcherChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipSwitcher(win->control->skip_switcher());
    });
    QObject::connect(win, &Win::captionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setTitle(caption(win));
    });

    QObject::connect(win, &Win::activeChanged, plasma_win, [plasma_win, win] {
        plasma_win->setActive(win->control->active());
    });
    QObject::connect(win, &Win::fullScreenChanged, plasma_win, [plasma_win, win] {
        plasma_win->setFullscreen(win->control->fullscreen());
    });
    QObject::connect(
        win, &Win::keepAboveChanged, plasma_win, &Wrapland::Server::PlasmaWindow::setKeepAbove);
    QObject::connect(
        win, &Win::keepBelowChanged, plasma_win, &Wrapland::Server::PlasmaWindow::setKeepBelow);
    QObject::connect(win, &Win::minimizedChanged, plasma_win, [plasma_win, win] {
        plasma_win->setMinimized(win->control->minimized());
    });
    QObject::connect(win,
                     static_cast<void (Win::*)(Toplevel*, win::maximize_mode)>(
                         &Win::clientMaximizedStateChanged),
                     plasma_win,
                     [plasma_win]([[maybe_unused]] Toplevel* c, win::maximize_mode mode) {
                         plasma_win->setMaximized(mode == win::maximize_mode::full);
                     });
    QObject::connect(win, &Win::demandsAttentionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setDemandsAttention(win->control->demands_attention());
    });
    QObject::connect(win, &Win::iconChanged, plasma_win, [plasma_win, win] {
        plasma_win->setIcon(win->control->icon());
    });
    QObject::connect(win, &Win::windowClassChanged, plasma_win, updateAppId);
    QObject::connect(win, &Win::desktopFileNameChanged, plasma_win, updateAppId);
    QObject::connect(win, &Win::transientChanged, plasma_win, [plasma_win, win] {
        auto lead = win->transient()->lead();
        if (lead && !lead->control) {
            // When lead becomes remnant.
            lead = nullptr;
        }
        plasma_win->setParentWindow(lead ? lead->control->plasma_wayland_integration : nullptr);
    });
    QObject::connect(win, &Win::applicationMenuChanged, plasma_win, [plasma_win, win] {
        auto const appmenu = win->control->application_menu();
        plasma_win->setApplicationMenuPaths(QString::fromStdString(appmenu.address.name),
                                            QString::fromStdString(appmenu.address.path));
    });
    QObject::connect(win, &Win::frame_geometry_changed, plasma_win, [plasma_win, win] {
        plasma_win->setGeometry(win->frameGeometry());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::closeRequested, win, [win] {
        win->closeWindow();
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::moveRequested, win, [win] {
        auto& cursor = win->space.input->platform.cursor;
        cursor->set_pos(win->frameGeometry().center());
        win->performMouseCommand(base::options_qobject::MouseMove, cursor->pos());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::resizeRequested, win, [win] {
        auto& cursor = win->space.input->platform.cursor;
        cursor->set_pos(win->frameGeometry().bottomRight());
        win->performMouseCommand(base::options_qobject::MouseResize, cursor->pos());
    });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::fullscreenRequested,
                     win,
                     [win](bool set) { win->setFullScreen(set, false); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::minimizedRequested, win, [win](bool set) {
            if (set) {
                set_minimized(win, true);
            } else {
                set_minimized(win, false);
            }
        });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::maximizedRequested, win, [win](bool set) {
            win::maximize(win, set ? win::maximize_mode::full : win::maximize_mode::restore);
        });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepAboveRequested,
                     win,
                     [win](bool set) { win::set_keep_above(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepBelowRequested,
                     win,
                     [win](bool set) { win::set_keep_below(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::demandsAttentionRequested,
                     win,
                     [win](bool set) { win::set_demands_attention(win, set); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::activeRequested, win, [win](bool set) {
            if (set) {
                force_activate_window(win->space, win);
            }
        });

    for (auto const vd : win->desktops()) {
        plasma_win->addPlasmaVirtualDesktop(vd->id().toStdString());
    }

    // Only for the legacy mechanism.
    QObject::connect(win, &Win::desktopChanged, plasma_win, [plasma_win, win] {
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
                         auto vd
                             = win->space.virtual_desktop_manager->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             enter_desktop(win, vd);
                         }
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterNewPlasmaVirtualDesktopRequested,
                     win,
                     [win]() {
                         auto& vds = win->space.virtual_desktop_manager;
                         vds->setCount(vds->count() + 1);
                         enter_desktop(win, vds->desktops().last());
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::leavePlasmaVirtualDesktopRequested,
                     win,
                     [win](const QString& desktopId) {
                         auto vd
                             = win->space.virtual_desktop_manager->desktopForId(desktopId.toUtf8());
                         if (vd) {
                             leave_desktop(win, vd);
                         }
                     });

    win->control->plasma_wayland_integration = plasma_win;
}

}
