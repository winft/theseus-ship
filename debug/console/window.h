/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/actions.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/desktop_get.h"
#include "win/meta.h"
#include "win/property_window.h"
#include "win/space.h"
#include "win/transient.h"

namespace KWin::debug
{

template<typename RefWin>
class console_window : public win::property_window
{
public:
    explicit console_window(RefWin* ref_win)
        : property_window(*ref_win->qobject)
        , ref_win{ref_win}
    {
    }

    xcb_window_t frameId() const override
    {
        return ref_win->frameId();
    }

    quint32 windowId() const override
    {
        return ref_win->xcb_window;
    }

    QByteArray resourceName() const override
    {
        return ref_win->resource_name;
    }

    QByteArray resourceClass() const override
    {
        return ref_win->resource_class;
    }

    QString caption() const override
    {
        return win::caption(ref_win);
    }

    QIcon icon() const override
    {
        if (!ref_win->control) {
            return {};
        }
        return ref_win->control->icon;
    }

    QRect iconGeometry() const override
    {
        if (!ref_win->control) {
            return QRect();
        }
        if (ref_win->control->icon.isNull()) {
            return QRect();
        }
        return ref_win->iconGeometry();
    }

    QUuid internalId() const override
    {
        return ref_win->internal_id;
    }

    pid_t pid() const override
    {
        if (!ref_win->info) {
            return 0;
        }
        return ref_win->pid();
    }

    QRect bufferGeometry() const override
    {
        return win::render_geometry(ref_win);
    }

    QRect frameGeometry() const override
    {
        return ref_win->frameGeometry();
    }

    void setFrameGeometry(QRect const& geo) override
    {
        if (ref_win->control) {
            ref_win->setFrameGeometry(geo);
        }
    }

    QPoint pos() const override
    {
        return ref_win->pos();
    }

    QRect rect() const override
    {
        return QRect(QPoint(0, 0), ref_win->size());
    }

    QRect visibleRect() const override
    {
        return win::visible_rect(ref_win);
    }

    QSize size() const override
    {
        return ref_win->size();
    }

    QSize minSize() const override
    {
        if (!ref_win->control) {
            return {};
        }
        return ref_win->minSize();
    }

    QSize maxSize() const override
    {
        if (!ref_win->control) {
            return {};
        }
        return ref_win->maxSize();
    }

    QPoint clientPos() const override
    {
        return win::frame_relative_client_rect(ref_win).topLeft();
    }

    QSize clientSize() const override
    {
        return win::frame_to_client_size(ref_win, ref_win->size());
    }

    int x() const override
    {
        return ref_win->pos().x();
    }

    int y() const override
    {
        return ref_win->pos().y();
    }

    int width() const override
    {
        return ref_win->size().width();
    }

    int height() const override
    {
        return ref_win->size().height();
    }

    bool isMove() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return win::is_move(ref_win);
    }

    bool isResize() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return win::is_resize(ref_win);
    }

    bool hasAlpha() const override
    {
        return ref_win->hasAlpha();
    }

    qreal opacity() const override
    {
        return ref_win->opacity();
    }

    void setOpacity(qreal opacity) override
    {
        if (ref_win->control) {
            ref_win->setOpacity(opacity);
        }
    }

    bool isFullScreen() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->fullscreen;
    }

    void setFullScreen(bool set) override
    {
        if (ref_win->control) {
            ref_win->setFullScreen(set);
        }
    }

    int screen() const override
    {
        if (!ref_win->central_output) {
            return 0;
        }
        return base::get_output_index(ref_win->space.base.outputs, *ref_win->central_output);
    }

    int desktop() const override
    {
        return ref_win->desktop();
    }

    void setDesktop(int desktop) override
    {
        if (ref_win->control) {
            win::set_desktop(ref_win, desktop);
        }
    }

    QVector<uint> x11DesktopIds() const override
    {
        return win::x11_desktop_ids(ref_win);
    }

    bool isOnAllDesktops() const override
    {
        return ref_win->isOnAllDesktops();
    }

    void setOnAllDesktops(bool set) override
    {
        if (ref_win->control) {
            win::set_on_all_desktops(ref_win, set);
        }
    }

    QByteArray windowRole() const override
    {
        return ref_win->windowRole();
    }

    NET::WindowType windowType() const override
    {
        return ref_win->windowType();
    }

    bool isDesktop() const override
    {
        return win::is_desktop(ref_win);
    }

    bool isDock() const override
    {
        return win::is_dock(ref_win);
    }

    bool isToolbar() const override
    {
        return win::is_toolbar(ref_win);
    }

    bool isMenu() const override
    {
        return win::is_menu(ref_win);
    }

    bool isNormalWindow() const override
    {
        return win::is_normal(ref_win);
    }

    bool isDialog() const override
    {
        return win::is_dialog(ref_win);
    }

    bool isSplash() const override
    {
        return win::is_splash(ref_win);
    }

    bool isUtility() const override
    {
        return win::is_utility(ref_win);
    }

    bool isDropdownMenu() const override
    {
        return win::is_dropdown_menu(ref_win);
    }

    bool isPopupMenu() const override
    {
        return win::is_popup_menu(ref_win);
    }

    bool isTooltip() const override
    {
        return win::is_tooltip(ref_win);
    }

    bool isNotification() const override
    {
        return win::is_notification(ref_win);
    }

    bool isCriticalNotification() const override
    {
        return win::is_critical_notification(ref_win);
    }

    bool isAppletPopup() const override
    {
        return win::is_applet_popup(ref_win);
    }

    bool isOnScreenDisplay() const override
    {
        return win::is_on_screen_display(ref_win);
    }

    bool isComboBox() const override
    {
        return win::is_combo_box(ref_win);
    }

    bool isDNDIcon() const override
    {
        return win::is_dnd_icon(ref_win);
    }

    bool isPopupWindow() const override
    {
        return win::is_popup(ref_win);
    }

    bool isSpecialWindow() const override
    {
        return win::is_special_window(ref_win);
    }

    bool isCloseable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isCloseable();
    }

    bool isMovable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isMovable();
    }

    bool isMovableAcrossScreens() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isMovableAcrossScreens();
    }

    bool isResizable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isResizable();
    }

    bool isMinimizable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isMinimizable();
    }

    bool isMaximizable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->isMaximizable();
    }

    bool isFullScreenable() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->can_fullscreen();
    }

    bool isOutline() const override
    {
        return ref_win->isOutline();
    }

    bool isShape() const override
    {
        return ref_win->is_shape;
    }

    bool keepAbove() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->keep_above;
    }

    void setKeepAbove(bool set) override
    {
        if (ref_win->control) {
            win::set_keep_above(ref_win, set);
        }
    }

    bool keepBelow() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->keep_below;
    }

    void setKeepBelow(bool set) override
    {
        if (ref_win->control) {
            win::set_keep_below(ref_win, set);
        }
    }

    bool isMinimized() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->minimized;
    }

    void setMinimized(bool set) override
    {
        if (ref_win->control) {
            win::set_minimized(ref_win, set);
        }
    }

    bool skipTaskbar() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->skip_taskbar();
    }

    void setSkipTaskbar(bool set) override
    {
        if (ref_win->control) {
            win::set_skip_taskbar(ref_win, set);
        }
    }

    bool skipPager() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->skip_pager();
    }

    void setSkipPager(bool set) override
    {
        if (ref_win->control) {
            win::set_skip_pager(ref_win, set);
        }
    }

    bool skipSwitcher() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->skip_switcher();
    }

    void setSkipSwitcher(bool set) override
    {
        if (ref_win->control) {
            win::set_skip_switcher(ref_win, set);
        }
    }

    bool skipsCloseAnimation() const override
    {
        return ref_win->skipsCloseAnimation();
    }

    void setSkipCloseAnimation(bool set) override
    {
        if (ref_win->control) {
            ref_win->setSkipCloseAnimation(set);
        }
    }

    bool isActive() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->active;
    }

    bool isDemandingAttention() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->demands_attention;
    }

    void demandAttention(bool set) override
    {
        if (ref_win->control) {
            win::set_demands_attention(ref_win, set);
        }
    }

    bool wantsInput() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->wantsInput();
    }

    bool applicationMenuActive() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->appmenu.active;
    }

    bool unresponsive() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->unresponsive;
    }

    bool isTransient() const override
    {
        return ref_win->transient()->lead();
    }

    console_window* transientFor() const override
    {
        // Not implemented.
        return nullptr;
    }

    bool isModal() const override
    {
        return ref_win->transient()->modal();
    }

    bool decorationHasAlpha() const override
    {
        return win::decoration_has_alpha(ref_win);
    }

    bool hasNoBorder() const override
    {
        if (!ref_win->control) {
            return true;
        }
        return ref_win->noBorder();
    }

    void setNoBorder(bool set) override
    {
        if (ref_win->control) {
            ref_win->setNoBorder(set);
        }
    }

    QString colorScheme() const override
    {
        if (!ref_win->control) {
            return {};
        }
        return ref_win->control->palette.color_scheme;
    }

    QByteArray desktopFileName() const override
    {
        if (!ref_win->control) {
            return {};
        }
        return ref_win->control->desktop_file_name;
    }

    bool hasApplicationMenu() const override
    {
        if (!ref_win->control) {
            return false;
        }
        return ref_win->control->has_application_menu();
    }

    bool providesContextHelp() const override
    {
        return ref_win->providesContextHelp();
    }

    bool isDeleted() const override
    {
        return static_cast<bool>(ref_win->remnant);
    }

    quint32 surfaceId() const override
    {
        return ref_win->surface_id;
    }

    Wrapland::Server::Surface* surface() const override
    {
        return ref_win->surface;
    }

    QSize basicUnit() const override
    {
        return ref_win->basicUnit();
    }

    bool isBlockingCompositing() override
    {
        return ref_win->isBlockingCompositing();
    }

    void setBlockingCompositing(bool block) override
    {
        if (ref_win->control) {
            ref_win->setBlockingCompositing(block);
        }
    }

    RefWin* ref_win;
};

}
