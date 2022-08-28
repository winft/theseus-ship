/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "space.h"

#include "toplevel.h"
#include "win/actions.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/desktop_get.h"
#include "win/meta.h"
#include "win/screen.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/x11/window.h"

namespace KWin::scripting
{

window::window(win::window_qobject& qtwin)
    : property_window(qtwin)
{
}

window_impl::window_impl(Toplevel* client, space* workspace)
    : window(*client->qobject)
    , m_client{client}
    , m_workspace{workspace}
{
    auto qtwin = get_window_qobject();
    QObject::connect(qtwin, &win::window_qobject::opacityChanged, this, [this](auto oldOpacity) {
        Q_EMIT opacityChanged(this, oldOpacity);
    });

    QObject::connect(qtwin,
                     &win::window_qobject::desktopPresenceChanged,
                     this,
                     [this](auto desktop) { Q_EMIT desktopPresenceChanged(this, desktop); });

    QObject::connect(qtwin, &win::window_qobject::clientMinimized, this, [this] {
        Q_EMIT clientMinimized(this);
    });
    QObject::connect(qtwin, &win::window_qobject::clientUnminimized, this, [this] {
        Q_EMIT clientUnminimized(this);
    });

    QObject::connect(qtwin, &win::window_qobject::maximize_mode_changed, this, [this](auto mode) {
        Q_EMIT clientMaximizedStateChanged(this,
                                           flags(mode & win::maximize_mode::horizontal),
                                           flags(mode & win::maximize_mode::vertical));
    });

    QObject::connect(
        qtwin, &win::window_qobject::quicktiling_changed, this, &window_impl::quickTileModeChanged);

    QObject::connect(
        qtwin, &win::window_qobject::paletteChanged, this, &window_impl::paletteChanged);
    QObject::connect(qtwin,
                     &win::window_qobject::moveResizeCursorChanged,
                     this,
                     &window_impl::moveResizeCursorChanged);
    QObject::connect(qtwin, &win::window_qobject::clientStartUserMovedResized, this, [this] {
        Q_EMIT clientStartUserMovedResized(this);
    });
    QObject::connect(qtwin,
                     &win::window_qobject::clientStepUserMovedResized,
                     this,
                     [this](auto rect) { Q_EMIT clientStepUserMovedResized(this, rect); });
    QObject::connect(qtwin, &win::window_qobject::clientFinishUserMovedResized, this, [this] {
        Q_EMIT clientFinishUserMovedResized(this);
    });

    QObject::connect(
        qtwin, &win::window_qobject::closeableChanged, this, &window_impl::closeableChanged);
    QObject::connect(
        qtwin, &win::window_qobject::minimizeableChanged, this, &window_impl::minimizeableChanged);
    QObject::connect(
        qtwin, &win::window_qobject::maximizeableChanged, this, &window_impl::maximizeableChanged);

    // For backwards compatibility of scripts connecting to the old signal. We assume no script is
    // actually differentiating its behavior on the user parameter (if fullscreen was triggered by
    // the user or not) and always set it to being a user change.
    QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, this, [this, client] {
        Q_EMIT clientFullScreenSet(this, client->control->fullscreen, true);
    });

    if (client->isClient()) {
        QObject::connect(
            qtwin, &win::window_qobject::blockingCompositingChanged, this, [this](auto /*block*/) {
                // TODO(romangg): Should we emit null if block is false?
                Q_EMIT blockingCompositingChanged(this);
            });
    }
}

xcb_window_t window_impl::frameId() const
{
    return m_client->frameId();
}

quint32 window_impl::windowId() const
{
    return m_client->xcb_window;
}

QByteArray window_impl::resourceName() const
{
    return m_client->resource_name;
}

QByteArray window_impl::resourceClass() const
{
    return m_client->resource_class;
}

QString window_impl::caption() const
{
    return win::caption(m_client);
}

QIcon window_impl::icon() const
{
    return m_client->control->icon;
}

QRect window_impl::iconGeometry() const
{
    return m_client->iconGeometry();
}

QUuid window_impl::internalId() const
{
    return m_client->internal_id;
}

pid_t window_impl::pid() const
{
    return m_client->pid();
}

QRect window_impl::bufferGeometry() const
{
    return win::render_geometry(m_client);
}

QRect window_impl::frameGeometry() const
{
    return m_client->frameGeometry();
}

void window_impl::setFrameGeometry(QRect const& geo)
{
    m_client->setFrameGeometry(geo);
}

QPoint window_impl::pos() const
{
    return m_client->pos();
}

QRect window_impl::rect() const
{
    return QRect(QPoint(0, 0), m_client->size());
}

QRect window_impl::visibleRect() const
{
    return win::visible_rect(m_client);
}

QSize window_impl::size() const
{
    return m_client->size();
}

QSize window_impl::minSize() const
{
    return m_client->minSize();
}

QSize window_impl::maxSize() const
{
    return m_client->maxSize();
}

QPoint window_impl::clientPos() const
{
    return win::frame_relative_client_rect(m_client).topLeft();
}

QSize window_impl::clientSize() const
{
    return win::frame_to_client_size(m_client, m_client->size());
}

int window_impl::x() const
{
    return m_client->pos().x();
}

int window_impl::y() const
{
    return m_client->pos().y();
}

int window_impl::width() const
{
    return m_client->size().width();
}

int window_impl::height() const
{
    return m_client->size().height();
}

bool window_impl::isMove() const
{
    return win::is_move(m_client);
}

bool window_impl::isResize() const
{
    return win::is_resize(m_client);
}

bool window_impl::hasAlpha() const
{
    return m_client->hasAlpha();
}

qreal window_impl::opacity() const
{
    return m_client->opacity();
}

void window_impl::setOpacity(qreal opacity)
{
    m_client->setOpacity(opacity);
}

bool window_impl::isFullScreen() const
{
    return m_client->control->fullscreen;
}

void window_impl::setFullScreen(bool set)
{
    m_client->setFullScreen(set);
}

int window_impl::screen() const
{
    if (!m_client->central_output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *m_client->central_output);
}

int window_impl::desktop() const
{
    return m_client->desktop();
}

void window_impl::setDesktop(int desktop)
{
    win::set_desktop(m_client, desktop);
}

QVector<uint> window_impl::x11DesktopIds() const
{
    return win::x11_desktop_ids(m_client);
}

bool window_impl::isOnAllDesktops() const
{
    return m_client->isOnAllDesktops();
}

void window_impl::setOnAllDesktops(bool set)
{
    win::set_on_all_desktops(m_client, set);
}

bool window_impl::isOnDesktop(unsigned int desktop) const
{
    return m_client->isOnDesktop(desktop);
}

bool window_impl::isOnCurrentDesktop() const
{
    return m_client->isOnCurrentDesktop();
}

QStringList window::activities() const
{
    return {};
}

QByteArray window_impl::windowRole() const
{
    return m_client->windowRole();
}

NET::WindowType window_impl::windowType(bool direct, int supported_types) const
{
    return m_client->windowType(direct, supported_types);
}

bool window_impl::isDesktop() const
{
    return win::is_desktop(m_client);
}

bool window_impl::isDock() const
{
    return win::is_dock(m_client);
}

bool window_impl::isToolbar() const
{
    return win::is_toolbar(m_client);
}

bool window_impl::isMenu() const
{
    return win::is_menu(m_client);
}

bool window_impl::isNormalWindow() const
{
    return win::is_normal(m_client);
}

bool window_impl::isDialog() const
{
    return win::is_dialog(m_client);
}

bool window_impl::isSplash() const
{
    return win::is_splash(m_client);
}

bool window_impl::isUtility() const
{
    return win::is_utility(m_client);
}

bool window_impl::isDropdownMenu() const
{
    return win::is_dropdown_menu(m_client);
}

bool window_impl::isPopupMenu() const
{
    return win::is_popup_menu(m_client);
}

bool window_impl::isTooltip() const
{
    return win::is_tooltip(m_client);
}

bool window_impl::isNotification() const
{
    return win::is_notification(m_client);
}

bool window_impl::isCriticalNotification() const
{
    return win::is_critical_notification(m_client);
}

bool window_impl::isOnScreenDisplay() const
{
    return win::is_on_screen_display(m_client);
}

bool window_impl::isComboBox() const
{
    return win::is_combo_box(m_client);
}

bool window_impl::isDNDIcon() const
{
    return win::is_dnd_icon(m_client);
}

bool window_impl::isPopupWindow() const
{
    return win::is_popup(m_client);
}

bool window_impl::isSpecialWindow() const
{
    return win::is_special_window(m_client);
}

bool window_impl::isCloseable() const
{
    return m_client->isCloseable();
}

bool window_impl::isMovable() const
{
    return m_client->isMovable();
}

bool window_impl::isMovableAcrossScreens() const
{
    return m_client->isMovableAcrossScreens();
}

bool window_impl::isResizable() const
{
    return m_client->isResizable();
}

bool window_impl::isMinimizable() const
{
    return m_client->isMinimizable();
}

bool window_impl::isMaximizable() const
{
    return m_client->isMaximizable();
}

bool window_impl::isFullScreenable() const
{
    return m_client->control->can_fullscreen();
}

bool window::isShadeable() const
{
    return false;
}

bool window_impl::isOutline() const
{
    return m_client->isOutline();
}

bool window_impl::isShape() const
{
    return m_client->is_shape;
}

bool window::isShade() const
{
    return false;
}

void window::setShade(bool /*set*/)
{
}

bool window_impl::keepAbove() const
{
    return m_client->control->keep_above;
}

void window_impl::setKeepAbove(bool set)
{
    win::set_keep_above(m_client, set);
}

bool window_impl::keepBelow() const
{
    return m_client->control->keep_below;
}

void window_impl::setKeepBelow(bool set)
{
    win::set_keep_below(m_client, set);
}

bool window_impl::isMinimized() const
{
    return m_client->control->minimized;
}

void window_impl::setMinimized(bool set)
{
    win::set_minimized(m_client, set);
}

bool window_impl::skipTaskbar() const
{
    return m_client->control->skip_taskbar();
}

void window_impl::setSkipTaskbar(bool set)
{
    win::set_skip_taskbar(m_client, set);
}

bool window_impl::skipPager() const
{
    return m_client->control->skip_pager();
}

void window_impl::setSkipPager(bool set)
{
    win::set_skip_pager(m_client, set);
}

bool window_impl::skipSwitcher() const
{
    return m_client->control->skip_switcher();
}

void window_impl::setSkipSwitcher(bool set)
{
    win::set_skip_switcher(m_client, set);
}

bool window_impl::skipsCloseAnimation() const
{
    return m_client->skipsCloseAnimation();
}

void window_impl::setSkipCloseAnimation(bool set)
{
    m_client->setSkipCloseAnimation(set);
}

bool window_impl::isActive() const
{
    return m_client->control->active;
}

bool window_impl::isDemandingAttention() const
{
    return m_client->control->demands_attention;
}

void window_impl::demandAttention(bool set)
{
    win::set_demands_attention(m_client, set);
}

bool window_impl::wantsInput() const
{
    return m_client->wantsInput();
}

bool window_impl::applicationMenuActive() const
{
    return m_client->control->appmenu.active;
}

bool window_impl::unresponsive() const
{
    return m_client->control->unresponsive;
}

bool window_impl::isTransient() const
{
    return m_client->transient()->lead();
}

window* window_impl::transientFor() const
{
    auto parent = m_client->transient()->lead();
    if (!parent) {
        return nullptr;
    }
    return m_workspace->get_window(parent);
}

bool window_impl::isModal() const
{
    return m_client->transient()->modal();
}

bool window_impl::decorationHasAlpha() const
{
    return win::decoration_has_alpha(m_client);
}

bool window_impl::hasNoBorder() const
{
    return m_client->noBorder();
}

void window_impl::setNoBorder(bool set)
{
    m_client->setNoBorder(set);
}

QString window_impl::colorScheme() const
{
    return m_client->control->palette.color_scheme;
}

QByteArray window_impl::desktopFileName() const
{
    return m_client->control->desktop_file_name;
}

bool window_impl::hasApplicationMenu() const
{
    return m_client->control->has_application_menu();
}

bool window_impl::providesContextHelp() const
{
    return m_client->providesContextHelp();
}

bool window_impl::isClient() const
{
    return m_client->isClient();
}

bool window_impl::isDeleted() const
{
    return static_cast<bool>(m_client->remnant);
}

quint32 window_impl::surfaceId() const
{
    return m_client->surface_id;
}

Wrapland::Server::Surface* window_impl::surface() const
{
    return m_client->surface;
}

QSize window_impl::basicUnit() const
{
    return m_client->basicUnit();
}

bool window_impl::isBlockingCompositing()
{
    return m_client->isBlockingCompositing();
}

void window_impl::setBlockingCompositing(bool block)
{
    m_client->setBlockingCompositing(block);
}

Toplevel* window_impl::client() const
{
    return m_client;
}

}
