/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/actions.h"
#include "win/activation.h"
#include "win/meta.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/types.h"

#include <Wrapland/Server/plasma_virtual_desktop.h>
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
    auto plasma_win = space->plasma_window_manager->createWindow(
        win->meta.internal_id.toString().toStdString());
    plasma_win->setTitle(win::caption(win));
    plasma_win->setActive(win->control->active);
    plasma_win->setFullscreen(win->control->fullscreen);
    plasma_win->setKeepAbove(win->control->keep_above);
    plasma_win->setKeepBelow(win->control->keep_below);
    plasma_win->setMaximized(win->maximizeMode() == win::maximize_mode::full);
    plasma_win->setMinimized(win->control->minimized);
    plasma_win->setDemandsAttention(win->control->demands_attention);
    plasma_win->setCloseable(win->isCloseable());
    plasma_win->setMaximizeable(win->isMaximizable());
    plasma_win->setMinimizeable(win->isMinimizable());
    plasma_win->setFullscreenable(win->control->can_fullscreen());
    plasma_win->setIcon(win->control->icon);
    auto updateAppId = [win, plasma_win] {
        auto const name = win->control->desktop_file_name;
        plasma_win->setAppId(
            QString::fromUtf8(name.isEmpty() ? win->meta.wm_class.res_class : name));
        plasma_win->set_resource_name(win->meta.wm_class.res_name.toStdString());
    };
    updateAppId();
    plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    plasma_win->setSkipSwitcher(win->control->skip_switcher());
    plasma_win->setPid(win->pid());
    plasma_win->setResizable(win->isResizable());
    plasma_win->setMovable(win->isMovable());
    auto const appmenu = win->control->appmenu;
    plasma_win->setApplicationMenuPaths(QString::fromStdString(appmenu.address.name),
                                        QString::fromStdString(appmenu.address.path));

    // FIXME Matches X11Client::actionSupported(), but both should be implemented.
    plasma_win->setVirtualDesktopChangeable(true);

    auto transient_lead = win->transient->lead();
    plasma_win->setParentWindow(transient_lead ? transient_lead->control->plasma_wayland_integration
                                               : nullptr);
    plasma_win->setGeometry(win->geo.frame);

    auto qtwin = win->qobject.get();
    QObject::connect(qtwin, &window_qobject::skipTaskbarChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipTaskbar(win->control->skip_taskbar());
    });
    QObject::connect(qtwin, &window_qobject::skipSwitcherChanged, plasma_win, [plasma_win, win] {
        plasma_win->setSkipSwitcher(win->control->skip_switcher());
    });
    QObject::connect(qtwin, &window_qobject::captionChanged, plasma_win, [plasma_win, win] {
        plasma_win->setTitle(caption(win));
    });

    QObject::connect(qtwin, &window_qobject::activeChanged, plasma_win, [plasma_win, win] {
        plasma_win->setActive(win->control->active);
    });
    QObject::connect(qtwin, &window_qobject::fullScreenChanged, plasma_win, [plasma_win, win] {
        plasma_win->setFullscreen(win->control->fullscreen);
    });
    QObject::connect(qtwin,
                     &window_qobject::keepAboveChanged,
                     plasma_win,
                     &Wrapland::Server::PlasmaWindow::setKeepAbove);
    QObject::connect(qtwin,
                     &window_qobject::keepBelowChanged,
                     plasma_win,
                     &Wrapland::Server::PlasmaWindow::setKeepBelow);
    QObject::connect(qtwin, &window_qobject::minimizedChanged, plasma_win, [plasma_win, win] {
        plasma_win->setMinimized(win->control->minimized);
    });
    QObject::connect(
        qtwin, &window_qobject::maximize_mode_changed, plasma_win, [plasma_win](auto mode) {
            plasma_win->setMaximized(mode == win::maximize_mode::full);
        });
    QObject::connect(
        qtwin, &window_qobject::demandsAttentionChanged, plasma_win, [plasma_win, win] {
            plasma_win->setDemandsAttention(win->control->demands_attention);
        });
    QObject::connect(qtwin, &window_qobject::iconChanged, plasma_win, [plasma_win, win] {
        plasma_win->setIcon(win->control->icon);
    });
    QObject::connect(qtwin, &window_qobject::windowClassChanged, plasma_win, updateAppId);
    QObject::connect(qtwin, &window_qobject::desktopFileNameChanged, plasma_win, updateAppId);
    QObject::connect(qtwin, &window_qobject::transientChanged, plasma_win, [plasma_win, win] {
        auto lead = win->transient->lead();
        if (lead && !lead->control) {
            // When lead becomes remnant.
            lead = nullptr;
        }
        plasma_win->setParentWindow(lead ? lead->control->plasma_wayland_integration : nullptr);
    });
    QObject::connect(qtwin, &window_qobject::applicationMenuChanged, plasma_win, [plasma_win, win] {
        auto const appmenu = win->control->appmenu;
        plasma_win->setApplicationMenuPaths(QString::fromStdString(appmenu.address.name),
                                            QString::fromStdString(appmenu.address.path));
    });
    QObject::connect(qtwin, &window_qobject::frame_geometry_changed, plasma_win, [plasma_win, win] {
        plasma_win->setGeometry(win->geo.frame);
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::closeRequested, qtwin, [win] {
        win->closeWindow();
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::moveRequested, qtwin, [win] {
        auto& cursor = win->space.input->cursor;
        cursor->set_pos(win->geo.frame.center());
        perform_mouse_command(*win, mouse_cmd::move, cursor->pos());
    });
    QObject::connect(plasma_win, &Wrapland::Server::PlasmaWindow::resizeRequested, qtwin, [win] {
        auto& cursor = win->space.input->cursor;
        cursor->set_pos(win->geo.frame.bottomRight());
        perform_mouse_command(*win, mouse_cmd::resize, cursor->pos());
    });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::fullscreenRequested,
                     win->qobject.get(),
                     [win](bool set) { win->setFullScreen(set, false); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::minimizedRequested, qtwin, [win](bool set) {
            if (set) {
                set_minimized(win, true);
            } else {
                set_minimized(win, false);
            }
        });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::maximizedRequested, qtwin, [win](bool set) {
            win::maximize(win, set ? win::maximize_mode::full : win::maximize_mode::restore);
        });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepAboveRequested,
                     qtwin,
                     [win](bool set) { win::set_keep_above(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::keepBelowRequested,
                     qtwin,
                     [win](bool set) { win::set_keep_below(win, set); });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::demandsAttentionRequested,
                     qtwin,
                     [win](bool set) { win::set_demands_attention(win, set); });
    QObject::connect(
        plasma_win, &Wrapland::Server::PlasmaWindow::activeRequested, qtwin, [win](bool set) {
            if (set) {
                force_activate_window(win->space, *win);
            }
        });

    for (auto const vd : win->topo.subspaces) {
        plasma_win->addPlasmaVirtualDesktop(vd->id().toStdString());
    }

    // We need to set `OnAllDesktops` after the actual VD list has been added.
    // Otherwise it will unconditionally add the current desktop to the interface
    // which may not be the case, for example, when using rules
    plasma_win->setOnAllDesktops(on_all_subspaces(*win));

    // Only for the legacy mechanism.
    QObject::connect(qtwin, &window_qobject::subspaces_changed, plasma_win, [plasma_win, win] {
        if (on_all_subspaces(*win)) {
            plasma_win->setOnAllDesktops(true);
            return;
        }
        plasma_win->setOnAllDesktops(false);
    });

    // Plasma Virtual desktop management
    // show/hide when the window enters/exits from desktop
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterPlasmaVirtualDesktopRequested,
                     qtwin,
                     [win](const QString& desktopId) {
                         if (auto vd = win->space.subspace_manager->subspace_for_id(desktopId)) {
                             enter_subspace(*win, vd);
                         }
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::enterNewPlasmaVirtualDesktopRequested,
                     qtwin,
                     [win]() {
                         auto& vds = win->space.subspace_manager;
                         vds->setCount(vds->subspaces.size() + 1);
                         enter_subspace(*win, vds->subspaces.back());
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::leavePlasmaVirtualDesktopRequested,
                     qtwin,
                     [win](const QString& desktopId) {
                         if (auto vd = win->space.subspace_manager->subspace_for_id(desktopId)) {
                             leave_subspace(*win, vd);
                         }
                     });
    QObject::connect(plasma_win,
                     &Wrapland::Server::PlasmaWindow::sendToOutputRequested,
                     qtwin,
                     [space, win](auto output) {
                         for (auto& out : win->space.base.outputs) {
                             if (out->wrapland_output() == output) {
                                 send_to_screen(*space, win, *out);
                                 break;
                             }
                         }
                     });

    win->control->plasma_wayland_integration = plasma_win;
}

template<typename Space>
void plasma_manage_update_stacking_order(Space& space)
{
    std::vector<uint32_t> ids;
    std::vector<std::string> uuids;

    for (auto win : space.stacking.order.stack) {
        std::visit(overload{[&](auto&& win) {
                       if (!win->control) {
                           return;
                       }
                       auto manage = win->control->plasma_wayland_integration;
                       if (!manage) {
                           return;
                       }
                       ids.push_back(manage->id());
                       uuids.push_back(manage->uuid());
                   }},
                   win);
    }

    space.plasma_window_manager->set_stacking_order(ids);
    space.plasma_window_manager->set_stacking_order_uuids(uuids);
}

template<typename Manager>
void setup_subspace_manager(Manager& manager,
                            Wrapland::Server::PlasmaVirtualDesktopManager& management)
{
    using namespace Wrapland::Server;

    Q_ASSERT(!manager.m_virtualDesktopManagement);
    manager.m_virtualDesktopManagement = &management;

    auto createPlasmaVirtualDesktop = [&manager](auto desktop) {
        auto pvd = manager.m_virtualDesktopManagement->createDesktop(
            desktop->id().toStdString(), desktop->x11DesktopNumber() - 1);
        pvd->setName(desktop->name().toStdString());
        pvd->sendDone();

        QObject::connect(desktop, &subspace::nameChanged, pvd, [desktop, pvd] {
            pvd->setName(desktop->name().toStdString());
            pvd->sendDone();
        });
        QObject::connect(pvd,
                         &PlasmaVirtualDesktop::activateRequested,
                         manager.qobject.get(),
                         [&manager, desktop] { manager.setCurrent(desktop); });
    };

    QObject::connect(manager.qobject.get(),
                     &decltype(manager.qobject)::element_type::subspace_created,
                     manager.m_virtualDesktopManagement,
                     createPlasmaVirtualDesktop);

    QObject::connect(manager.qobject.get(),
                     &decltype(manager.qobject)::element_type::rowsChanged,
                     manager.m_virtualDesktopManagement,
                     [&manager](uint rows) {
                         manager.m_virtualDesktopManagement->setRows(rows);
                         manager.m_virtualDesktopManagement->sendDone();
                     });

    // handle removed: from subspace_manager to the wayland interface
    QObject::connect(manager.qobject.get(),
                     &decltype(manager.qobject)::element_type::subspace_removed,
                     manager.m_virtualDesktopManagement,
                     [&manager](auto desktop) {
                         manager.m_virtualDesktopManagement->removeDesktop(
                             desktop->id().toStdString());
                     });

    // create a new desktop when the client asks to
    QObject::connect(manager.m_virtualDesktopManagement,
                     &PlasmaVirtualDesktopManager::desktopCreateRequested,
                     manager.qobject.get(),
                     [&manager](auto const& name, quint32 position) {
                         manager.create_subspace(position, QString::fromStdString(name));
                     });

    // remove when the client asks to
    QObject::connect(manager.m_virtualDesktopManagement,
                     &PlasmaVirtualDesktopManager::desktopRemoveRequested,
                     manager.qobject.get(),
                     [&manager](auto const& id) {
                         // here there can be some nice kauthorized check?
                         // remove only from subspace_manager, the other connections will
                         // remove it from m_virtualDesktopManagement as well
                         manager.remove_subspace(id.c_str());
                     });

    auto const& subspaces = manager.subspaces;
    std::for_each(subspaces.cbegin(), subspaces.cend(), createPlasmaVirtualDesktop);

    // Now we are sure all ids are there
    manager.save();

    QObject::connect(manager.qobject.get(),
                     &decltype(manager.qobject)::element_type::current_changed,
                     manager.m_virtualDesktopManagement,
                     [&manager]() {
                         for (auto deskInt : manager.m_virtualDesktopManagement->desktops()) {
                             if (deskInt->id() == manager.current->id().toStdString()) {
                                 deskInt->setActive(true);
                             } else {
                                 deskInt->setActive(false);
                             }
                         }
                     });
}

}
