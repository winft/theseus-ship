/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "toplevel.h"
#include "win/actions.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/desktop_get.h"
#include "win/meta.h"
#include "win/space.h"
#include "win/transient.h"

namespace KWin::debug
{

console_window::console_window(Toplevel* ref_win)
    : property_window(*ref_win->qobject)
    , ref_win{ref_win}
{
}

xcb_window_t console_window::frameId() const
{
    return ref_win->frameId();
}

quint32 console_window::windowId() const
{
    return ref_win->xcb_window;
}

QByteArray console_window::resourceName() const
{
    return ref_win->resource_name;
}

QByteArray console_window::resourceClass() const
{
    return ref_win->resource_class;
}

QString console_window::caption() const
{
    return win::caption(ref_win);
}

QIcon console_window::icon() const
{
    if (!ref_win->control) {
        return {};
    }
    return ref_win->control->icon;
}

QRect console_window::iconGeometry() const
{
    return ref_win->iconGeometry();
}

QUuid console_window::internalId() const
{
    return ref_win->internal_id;
}

pid_t console_window::pid() const
{
    return ref_win->pid();
}

QRect console_window::bufferGeometry() const
{
    return win::render_geometry(ref_win);
}

QRect console_window::frameGeometry() const
{
    return ref_win->frameGeometry();
}

void console_window::setFrameGeometry(QRect const& geo)
{
    if (ref_win->control) {
        ref_win->setFrameGeometry(geo);
    }
}

QPoint console_window::pos() const
{
    return ref_win->pos();
}

QRect console_window::rect() const
{
    return QRect(QPoint(0, 0), ref_win->size());
}

QRect console_window::visibleRect() const
{
    return win::visible_rect(ref_win);
}

QSize console_window::size() const
{
    return ref_win->size();
}

QSize console_window::minSize() const
{
    if (!ref_win->control) {
        return {};
    }
    return ref_win->minSize();
}

QSize console_window::maxSize() const
{
    if (!ref_win->control) {
        return {};
    }
    return ref_win->maxSize();
}

QPoint console_window::clientPos() const
{
    return win::frame_relative_client_rect(ref_win).topLeft();
}

QSize console_window::clientSize() const
{
    return win::frame_to_client_size(ref_win, ref_win->size());
}

int console_window::x() const
{
    return ref_win->pos().x();
}

int console_window::y() const
{
    return ref_win->pos().y();
}

int console_window::width() const
{
    return ref_win->size().width();
}

int console_window::height() const
{
    return ref_win->size().height();
}

bool console_window::isMove() const
{
    if (!ref_win->control) {
        return false;
    }
    return win::is_move(ref_win);
}

bool console_window::isResize() const
{
    if (!ref_win->control) {
        return false;
    }
    return win::is_resize(ref_win);
}

bool console_window::hasAlpha() const
{
    return ref_win->hasAlpha();
}

qreal console_window::opacity() const
{
    return ref_win->opacity();
}

void console_window::setOpacity(qreal opacity)
{
    if (ref_win->control) {
        ref_win->setOpacity(opacity);
    }
}

bool console_window::isFullScreen() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->fullscreen;
}

void console_window::setFullScreen(bool set)
{
    if (ref_win->control) {
        ref_win->setFullScreen(set);
    }
}

int console_window::screen() const
{
    if (!ref_win->central_output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *ref_win->central_output);
}

int console_window::desktop() const
{
    return ref_win->desktop();
}

void console_window::setDesktop(int desktop)
{
    if (ref_win->control) {
        win::set_desktop(ref_win, desktop);
    }
}

QVector<uint> console_window::x11DesktopIds() const
{
    return win::x11_desktop_ids(ref_win);
}

bool console_window::isOnAllDesktops() const
{
    return ref_win->isOnAllDesktops();
}

void console_window::setOnAllDesktops(bool set)
{
    if (ref_win->control) {
        win::set_on_all_desktops(ref_win, set);
    }
}

QByteArray console_window::windowRole() const
{
    return ref_win->windowRole();
}

NET::WindowType console_window::windowType(bool direct, int supported_types) const
{
    return ref_win->windowType(direct, supported_types);
}

bool console_window::isDesktop() const
{
    return win::is_desktop(ref_win);
}

bool console_window::isDock() const
{
    return win::is_dock(ref_win);
}

bool console_window::isToolbar() const
{
    return win::is_toolbar(ref_win);
}

bool console_window::isMenu() const
{
    return win::is_menu(ref_win);
}

bool console_window::isNormalWindow() const
{
    return win::is_normal(ref_win);
}

bool console_window::isDialog() const
{
    return win::is_dialog(ref_win);
}

bool console_window::isSplash() const
{
    return win::is_splash(ref_win);
}

bool console_window::isUtility() const
{
    return win::is_utility(ref_win);
}

bool console_window::isDropdownMenu() const
{
    return win::is_dropdown_menu(ref_win);
}

bool console_window::isPopupMenu() const
{
    return win::is_popup_menu(ref_win);
}

bool console_window::isTooltip() const
{
    return win::is_tooltip(ref_win);
}

bool console_window::isNotification() const
{
    return win::is_notification(ref_win);
}

bool console_window::isCriticalNotification() const
{
    return win::is_critical_notification(ref_win);
}

bool console_window::isOnScreenDisplay() const
{
    return win::is_on_screen_display(ref_win);
}

bool console_window::isComboBox() const
{
    return win::is_combo_box(ref_win);
}

bool console_window::isDNDIcon() const
{
    return win::is_dnd_icon(ref_win);
}

bool console_window::isPopupWindow() const
{
    return win::is_popup(ref_win);
}

bool console_window::isSpecialWindow() const
{
    return win::is_special_window(ref_win);
}

bool console_window::isCloseable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isCloseable();
}

bool console_window::isMovable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isMovable();
}

bool console_window::isMovableAcrossScreens() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isMovableAcrossScreens();
}

bool console_window::isResizable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isResizable();
}

bool console_window::isMinimizable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isMinimizable();
}

bool console_window::isMaximizable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->isMaximizable();
}

bool console_window::isFullScreenable() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->can_fullscreen();
}

bool console_window::isOutline() const
{
    return ref_win->isOutline();
}

bool console_window::isShape() const
{
    return ref_win->is_shape;
}

bool console_window::keepAbove() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->keep_above;
}

void console_window::setKeepAbove(bool set)
{
    if (ref_win->control) {
        win::set_keep_above(ref_win, set);
    }
}

bool console_window::keepBelow() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->keep_below;
}

void console_window::setKeepBelow(bool set)
{
    if (ref_win->control) {
        win::set_keep_below(ref_win, set);
    }
}

bool console_window::isMinimized() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->minimized;
}

void console_window::setMinimized(bool set)
{
    if (ref_win->control) {
        win::set_minimized(ref_win, set);
    }
}

bool console_window::skipTaskbar() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->skip_taskbar();
}

void console_window::setSkipTaskbar(bool set)
{
    if (ref_win->control) {
        win::set_skip_taskbar(ref_win, set);
    }
}

bool console_window::skipPager() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->skip_pager();
}

void console_window::setSkipPager(bool set)
{
    if (ref_win->control) {
        win::set_skip_pager(ref_win, set);
    }
}

bool console_window::skipSwitcher() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->skip_switcher();
}

void console_window::setSkipSwitcher(bool set)
{
    if (ref_win->control) {
        win::set_skip_switcher(ref_win, set);
    }
}

bool console_window::skipsCloseAnimation() const
{
    return ref_win->skipsCloseAnimation();
}

void console_window::setSkipCloseAnimation(bool set)
{
    if (ref_win->control) {
        ref_win->setSkipCloseAnimation(set);
    }
}

bool console_window::isActive() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->active;
}

bool console_window::isDemandingAttention() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->demands_attention;
}

void console_window::demandAttention(bool set)
{
    if (ref_win->control) {
        win::set_demands_attention(ref_win, set);
    }
}

bool console_window::wantsInput() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->wantsInput();
}

bool console_window::applicationMenuActive() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->appmenu.active;
}

bool console_window::unresponsive() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->unresponsive;
}

bool console_window::isTransient() const
{
    return ref_win->transient()->lead();
}

console_window* console_window::transientFor() const
{
    // Not implemented.
    return nullptr;
}

bool console_window::isModal() const
{
    return ref_win->transient()->modal();
}

bool console_window::decorationHasAlpha() const
{
    return win::decoration_has_alpha(ref_win);
}

bool console_window::hasNoBorder() const
{
    if (!ref_win->control) {
        return true;
    }
    return ref_win->noBorder();
}

void console_window::setNoBorder(bool set)
{
    if (ref_win->control) {
        ref_win->setNoBorder(set);
    }
}

QString console_window::colorScheme() const
{
    if (!ref_win->control) {
        return {};
    }
    return ref_win->control->palette.color_scheme;
}

QByteArray console_window::desktopFileName() const
{
    if (!ref_win->control) {
        return {};
    }
    return ref_win->control->desktop_file_name;
}

bool console_window::hasApplicationMenu() const
{
    if (!ref_win->control) {
        return false;
    }
    return ref_win->control->has_application_menu();
}

bool console_window::providesContextHelp() const
{
    return ref_win->providesContextHelp();
}

bool console_window::isDeleted() const
{
    return static_cast<bool>(ref_win->remnant);
}

quint32 console_window::surfaceId() const
{
    return ref_win->surface_id;
}

Wrapland::Server::Surface* console_window::surface() const
{
    return ref_win->surface;
}

QSize console_window::basicUnit() const
{
    return ref_win->basicUnit();
}

bool console_window::isBlockingCompositing()
{
    return ref_win->isBlockingCompositing();
}

void console_window::setBlockingCompositing(bool block)
{
    if (ref_win->control) {
        ref_win->setBlockingCompositing(block);
    }
}

}
