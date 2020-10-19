/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_H
#define KWIN_WIN_H

#include "input.h"
#include "move.h"
#include "net.h"
#include "types.h"

#include "abstract_client.h"
#include "appmenu.h"
#include "atoms.h"
#include "decorations/decorationbridge.h"
#include "effects.h"
#include "main.h"
#include "screen.h"
#include "shadow.h"
#include "utils.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"

#include <KDecoration2/Decoration>
#include <QList>
#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

inline bool compositing()
{
    return Workspace::self() && Workspace::self()->compositing();
}

inline Xcb::Property fetch_skip_close_animation(xcb_window_t window)
{
    return Xcb::Property(false, window, atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

/**
 * Call once before loop, is indirect.
 */
template<typename Win>
QList<Win*> all_main_clients(Win const* win)
{
    auto ret = win->mainClients();

    for (auto const cl : qAsConst(ret)) {
        ret += all_main_clients(cl);
    }

    return ret;
}

template<typename Win>
auto scene_window(Win* win)
{
    auto eff_win = win->effectWindow();
    return eff_win ? eff_win->sceneWindow() : nullptr;
}

/**
 * Returns the pointer to the window's shadow. A shadow is only available if Compositing is enabled
 * and on X11 if the corresponding X window has the shadow property set.
 *
 * @returns The shadow belonging to @param win, @c null if there's no shadow.
 */
template<typename Win>
auto shadow(Win* win)
{
    auto sc_win = scene_window(win);
    return sc_win ? sc_win->shadow() : nullptr;
}

/**
 * Updates the shadow associated with @param win.
 * Call this method when the windowing system notifies a change or compositing is started.
 */
template<typename Win>
auto update_shadow(Win* win)
{
    // Old & new shadow region
    QRect dirty_rect;

    auto const old_visible_rect = win->visibleRect();

    if (auto shdw = shadow(win)) {
        dirty_rect = shdw->shadowRegion().boundingRect();
        if (!shdw->updateShadow()) {
            scene_window(win)->updateShadow(nullptr);
        }
        Q_EMIT win->shadowChanged();
    } else if (win->effectWindow()) {
        Shadow::createShadow(win);
    }

    if (auto shdw = shadow(win)) {
        dirty_rect |= shdw->shadowRegion().boundingRect();
    }

    if (old_visible_rect != win->visibleRect()) {
        Q_EMIT win->paddingChanged(win, old_visible_rect);
    }

    if (dirty_rect.isValid()) {
        dirty_rect.translate(win->pos());
        win->addLayerRepaint(dirty_rect);
    }
}

/**
 * Window will be temporarily painted as if being at the top of the stack.
 * Only available if Compositor is active, if not active, this method is a no-op.
 */
template<typename Win>
void elevate(Win* win, bool elevate)
{
    if (auto effect_win = win->effectWindow()) {
        effect_win->elevate(elevate);
        win->addWorkspaceRepaint(win->visibleRect());
    }
}

template<typename Win>
void set_shade(Win* win, bool set)
{
    set ? win->setShade(ShadeNormal) : win->setShade(ShadeNone);
}

template<typename Win>
void send_to_screen(Win* win, int new_screen)
{
#ifndef KWIN_UNIT_TEST
    new_screen = win->rules()->checkScreen(new_screen);
#endif
    if (win->isActive()) {
        screens()->setCurrent(new_screen);
        // might impact the layer of a fullscreen window
        for (auto cc : workspace()->allClientList()) {
            if (cc->isFullScreen() && cc->screen() == new_screen) {
                cc->updateLayer();
            }
        }
    }
    if (win->screen() == new_screen) {
        // Don't use isOnScreen(), that's true even when only partially.
        return;
    }

    geometry_updates_blocker blocker(win);

    // operating on the maximized / quicktiled window would leave the old geom_restore behind,
    // so we clear the state first
    auto max_mode = win->maximizeMode();
    QuickTileMode qtMode = win->quickTileMode();
    if (max_mode != maximize_mode::restore) {
        win::maximize(win, win::maximize_mode::restore);
    }

    if (qtMode != QuickTileMode(QuickTileFlag::None)) {
        set_quicktile_mode(win, QuickTileFlag::None, true);
    }

    auto oldScreenArea = workspace()->clientArea(MaximizeArea, win);
    auto screenArea = workspace()->clientArea(MaximizeArea, new_screen, win->desktop());

    // the window can have its center so that the position correction moves the new center onto
    // the old screen, what will tile it where it is. Ie. the screen is not changed
    // this happens esp. with electric border quicktiling
    if (qtMode != QuickTileMode(QuickTileFlag::None)) {
        keep_in_area(win, oldScreenArea, false);
    }

    auto oldGeom = win->frameGeometry();
    auto newGeom = oldGeom;
    // move the window to have the same relative position to the center of the screen
    // (i.e. one near the middle of the right edge will also end up near the middle of the right
    // edge)
    QPoint center = newGeom.center() - oldScreenArea.center();
    center.setX(center.x() * screenArea.width() / oldScreenArea.width());
    center.setY(center.y() * screenArea.height() / oldScreenArea.height());
    center += screenArea.center();
    newGeom.moveCenter(center);
    win->setFrameGeometry(newGeom);

    // If the window was inside the old screen area, explicitly make sure its inside also the new
    // screen area. Calling checkWorkspacePosition() should ensure that, but when moving to a small
    // screen the window could be big enough to overlap outside of the new screen area, making
    // struts from other screens come into effect, which could alter the resulting geometry.
    if (oldScreenArea.contains(oldGeom)) {
        keep_in_area(win, screenArea, false);
    }

    // align geom_restore - checkWorkspacePosition operates on it
    win->setGeometryRestore(win->frameGeometry());

    check_workspace_position(win, oldGeom);

    // re-align geom_restore to constrained geometry
    win->setGeometryRestore(win->frameGeometry());

    // finally reset special states
    // NOTICE that MaximizeRestore/QuickTileFlag::None checks are required.
    // eg. setting QuickTileFlag::None would break maximization
    if (max_mode != maximize_mode::restore) {
        maximize(win, max_mode);
    }

    if (qtMode != QuickTileMode(QuickTileFlag::None) && qtMode != win->quickTileMode()) {
        set_quicktile_mode(win, qtMode, true);
    }

    auto tso = workspace()->ensureStackingOrder(win->transients());
    for (auto it = tso.constBegin(), end = tso.constEnd(); it != end; ++it) {
        send_to_screen(*it, new_screen);
    }
}

template<typename Win>
bool is_popup(Win* win)
{
    switch (win->windowType()) {
    case NET::ComboBox:
    case NET::DropdownMenu:
    case NET::PopupMenu:
    case NET::Tooltip:
        return true;
    default:
        return win->is_popup_end();
    }
}

template<typename Win>
bool is_active_fullscreen(Win const* win)
{
    if (!win->isFullScreen()) {
        return false;
    }

    // Instead of activeClient() - avoids flicker.
    auto const ac = workspace()->mostRecentlyActivatedClient();

    // According to NETWM spec implementation notes suggests "focused windows having state
    // _NET_WM_STATE_FULLSCREEN" to be on the highest layer. Also take the screen into account.
    return ac
        && (ac == win || ac->screen() != win->screen()
            || all_main_clients(ac).contains(const_cast<Win*>(win)));
}

/**
 * Tells if @p win is "special", in contrast normal windows are with a border, can be moved by the
 * user, can be closed, etc.
 */
template<typename Win>
bool is_special_window(Win* win)
{
    return is_desktop(win) || is_dock(win) || is_splash(win) || is_toolbar(win)
        || is_notification(win) || is_critical_notification(win) || is_on_screen_display(win);
}

/**
 * Looks for another AbstractClient with same captionNormal and captionSuffix.
 * If no such AbstractClient exists @c nullptr is returned.
 */
template<typename Win>
Win* find_client_with_same_caption(Win const* win)
{
    auto fetchNameInternalPredicate = [win](Win const* cl) {
        return (!is_special_window(cl) || is_toolbar(cl)) && cl != win
            && cl->captionNormal() == win->captionNormal()
            && cl->captionSuffix() == win->captionSuffix();
    };
    return workspace()->findAbstractClient(fetchNameInternalPredicate);
}

template<typename Win>
QString shortcut_caption_suffix(Win* win)
{
    if (win->shortcut().isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + win->shortcut().toString() + QLatin1Char('}');
}

/**
 * Request showing the application menu bar.
 * @param actionId The DBus menu ID of the action that should be highlighted, 0 for the root menu.
 */
template<typename Win>
void show_application_menu(Win* win, int actionId)
{
    if (win->isDecorated()) {
        win->decoration()->showApplicationMenu(actionId);
    } else {
        // No info where application menu button is, show it in the top left corner by default.
        Workspace::self()->showApplicationMenu(QRect(), win, actionId);
    }
}

template<typename Win>
bool decoration_has_alpha(Win* win)
{
    return win->isDecorated() && !win->decoration()->isOpaque();
}

template<typename Win>
void trigger_decoration_repaint(Win* win)
{
    if (win->isDecorated()) {
        win->decoration()->update();
    }
}

template<typename Win>
void layout_decoration_rects(Win* win, QRect& left, QRect& top, QRect& right, QRect& bottom)
{
    if (!win->isDecorated()) {
        return;
    }
    auto rect = win->decoration()->rect();

    top = QRect(rect.x(), rect.y(), rect.width(), top_border(win));
    bottom = QRect(
        rect.x(), rect.y() + rect.height() - bottom_border(win), rect.width(), bottom_border(win));
    left = QRect(rect.x(),
                 rect.y() + top.height(),
                 left_border(win),
                 rect.height() - top.height() - bottom.height());
    right = QRect(rect.x() + rect.width() - right_border(win),
                  rect.y() + top.height(),
                  right_border(win),
                  rect.height() - top.height() - bottom.height());
}

template<typename Win>
Layer belong_to_layer(Win* win)
{
    // NOTICE while showingDesktop, desktops move to the AboveLayer
    // (interchangeable w/ eg. yakuake etc. which will at first remain visible)
    // and the docks move into the NotificationLayer (which is between Above- and
    // ActiveLayer, so that active fullscreen windows will still cover everything)
    // Since the desktop is also activated, nothing should be in the ActiveLayer, though
    if (win->isInternal()) {
        return UnmanagedLayer;
    }
    if (win->isLockScreen()) {
        return UnmanagedLayer;
    }
    if (is_desktop(win)) {
        return workspace()->showingDesktop() ? AboveLayer : DesktopLayer;
    }
    if (is_splash(win)) {  // no damn annoying splashscreens
        return NormalLayer; // getting in the way of everything else
    }
    if (is_dock(win)) {
        if (workspace()->showingDesktop()) {
            return NotificationLayer;
        }
        return win->layerForDock();
    }
    if (is_on_screen_display(win)) {
        return OnScreenDisplayLayer;
    }
    if (is_notification(win)) {
        return NotificationLayer;
    }
    if (is_critical_notification(win)) {
        return CriticalNotificationLayer;
    }
    if (workspace()->showingDesktop() && win->belongsToDesktop()) {
        return AboveLayer;
    }
    if (win->keepBelow()) {
        return BelowLayer;
    }
    if (is_active_fullscreen(win)) {
        return ActiveLayer;
    }
    if (win->keepAbove()) {
        return AboveLayer;
    }
    return NormalLayer;
}

template<typename Win>
void update_layer(Win* win)
{
    if (win->layer() == belong_to_layer(win)) {
        return;
    }
    StackingUpdatesBlocker blocker(workspace());

    // Invalidate, will be updated when doing restacking.
    win->invalidateLayer();

    for (auto it = win->transients().constBegin(), end = win->transients().constEnd(); it != end;
         ++it) {
        (*it)->updateLayer();
    }
}

/**
 * Schedules a repaint for the visible rectangle before and after a
 * geometry update. The current visible rectangle is stored for the
 * next time this method is called as the before-geometry.
 */
template<typename Win>
void add_repaint_during_geometry_updates(Win* win)
{
    auto const deco_rect = win->visibleRect();
    win->addLayerRepaint(win->visible_rect_before_geometry_update());

    // Trigger repaint of window's new location.
    win->addLayerRepaint(deco_rect);
    win->set_visible_rect_before_geometry_update(deco_rect);
}

/**
 * @brief Finds the window matching the condition expressed in @p func in @p list.
 *
 * @param list The list to search in.
 * @param func The condition function (compare std::find_if).
 * @return The found window or @c null if there is no matching window.
 */
template<class Win, class W>
Win* find_in_list(const QList<Win*>& list, std::function<bool(W const*)> func)
{
    static_assert(std::is_base_of<W, Win>::value, "W must be derived from Win");

    const auto it = std::find_if(list.begin(), list.end(), func);
    if (it == list.end()) {
        return nullptr;
    }
    return *it;
}

template<typename Win1, typename Win2>
bool belong_to_same_client(Win1 win1,
                           Win2 win2,
                           same_client_check checks = flags<same_client_check>())
{
    return win1->belongsToSameApplication(win2, checks);
}

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

    QObject::connect(
        win, &Win::paletteChanged, win, [win] { win::trigger_decoration_repaint(win); });

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
