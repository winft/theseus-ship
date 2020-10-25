/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_wrapper.h"

#include "toplevel.h"
#include "win/win.h"
#include "workspace_wrapper.h"
#include "x11client.h"

namespace KWin
{

WindowWrapper::WindowWrapper(AbstractClient* client, WorkspaceWrapper* workspace)
    : m_client{client}
    , m_workspace{workspace}
{
    connect(
        client, &AbstractClient::clientMinimized, this, [this] { Q_EMIT clientMinimized(this); });
    connect(client, &AbstractClient::clientUnminimized, this, [this] {
        Q_EMIT clientUnminimized(this);
    });
    connect(client,
            qOverload<AbstractClient*, bool, bool>(&AbstractClient::clientMaximizedStateChanged),
            this,
            [this]([[maybe_unused]] AbstractClient* client, bool horizontal, bool vertical) {
                Q_EMIT clientMaximizedStateChanged(this, horizontal, vertical);
            });

    if (client->isClient()) {
        auto x11_client = dynamic_cast<X11Client*>(m_client);
        connect(
            x11_client, &X11Client::clientManaging, this, [this] { Q_EMIT clientManaging(this); });
        connect(x11_client,
                &X11Client::clientFullScreenSet,
                this,
                [this]([[maybe_unused]] X11Client* client, bool fullscreen, bool user) {
                    Q_EMIT clientFullscreenSet(this, fullscreen, user);
                });
        connect(client, &AbstractClient::blockingCompositingChanged, this, [this] {
            Q_EMIT blockingCompositingChanged(this);
        });
    }
}

xcb_window_t WindowWrapper::frameId() const
{
    return m_client->frameId();
}

quint32 WindowWrapper::windowId() const
{
    return m_client->windowId();
}

QByteArray WindowWrapper::resourceName() const
{
    return m_client->resourceName();
}

QByteArray WindowWrapper::resourceClass() const
{
    return m_client->resourceClass();
}

QString WindowWrapper::caption() const
{
    return win::caption(m_client);
}

QIcon WindowWrapper::icon() const
{
    return m_client->control()->icon();
}

QRect WindowWrapper::iconGeometry() const
{
    return m_client->iconGeometry();
}

QUuid WindowWrapper::internalId() const
{
    return m_client->internalId();
}

pid_t WindowWrapper::pid() const
{
    return m_client->pid();
}

QRect WindowWrapper::bufferGeometry() const
{
    return m_client->bufferGeometry();
}

QRect WindowWrapper::frameGeometry() const
{
    return m_client->frameGeometry();
}

void WindowWrapper::setFrameGeometry(QRect const& geo)
{
    m_client->setFrameGeometry(geo);
}

QPoint WindowWrapper::pos() const
{
    return m_client->pos();
}

QRect WindowWrapper::rect() const
{
    return m_client->rect();
}

QRect WindowWrapper::visibleRect() const
{
    return m_client->visibleRect();
}

QSize WindowWrapper::size() const
{
    return m_client->size();
}

QSize WindowWrapper::minSize() const
{
    return m_client->minSize();
}

QSize WindowWrapper::maxSize() const
{
    return m_client->maxSize();
}

QPoint WindowWrapper::clientPos() const
{
    return m_client->clientPos();
}

QSize WindowWrapper::clientSize() const
{
    return m_client->clientSize();
}

int WindowWrapper::x() const
{
    return m_client->x();
}

int WindowWrapper::y() const
{
    return m_client->y();
}

int WindowWrapper::width() const
{
    return m_client->width();
}

int WindowWrapper::height() const
{
    return m_client->height();
}

bool WindowWrapper::isMove() const
{
    return win::is_move(m_client);
}

bool WindowWrapper::isResize() const
{
    return win::is_resize(m_client);
}

bool WindowWrapper::hasAlpha() const
{
    return m_client->hasAlpha();
}

qreal WindowWrapper::opacity() const
{
    return m_client->opacity();
}

void WindowWrapper::setOpacity(qreal opacity)
{
    m_client->setOpacity(opacity);
}

bool WindowWrapper::isFullscreen() const
{
    return m_client->isFullScreen();
}

void WindowWrapper::setFullscreen(bool set)
{
    m_client->setFullScreen(set);
}

int WindowWrapper::screen() const
{
    return m_client->screen();
}

int WindowWrapper::desktop() const
{
    return m_client->desktop();
}

void WindowWrapper::setDesktop(int desktop)
{
    win::set_desktop(m_client, desktop);
}

QVector<uint> WindowWrapper::x11DesktopIds() const
{
    return m_client->x11DesktopIds();
}

bool WindowWrapper::isOnAllDesktops() const
{
    return m_client->isOnAllDesktops();
}

void WindowWrapper::setOnAllDesktops(bool set)
{
    win::set_on_all_desktops(m_client, set);
}

QStringList WindowWrapper::activities() const
{
    return m_client->activities();
}

QByteArray WindowWrapper::windowRole() const
{
    return m_client->windowRole();
}

NET::WindowType WindowWrapper::windowType(bool direct, int supported_types) const
{
    return m_client->windowType(direct, supported_types);
}

bool WindowWrapper::isDesktop() const
{
    return win::is_desktop(m_client);
}

bool WindowWrapper::isDock() const
{
    return win::is_dock(m_client);
}

bool WindowWrapper::isToolbar() const
{
    return win::is_toolbar(m_client);
}

bool WindowWrapper::isMenu() const
{
    return win::is_menu(m_client);
}

bool WindowWrapper::isNormalWindow() const
{
    return win::is_normal(m_client);
}

bool WindowWrapper::isDialog() const
{
    return win::is_dialog(m_client);
}

bool WindowWrapper::isSplash() const
{
    return win::is_splash(m_client);
}

bool WindowWrapper::isUtility() const
{
    return win::is_utility(m_client);
}

bool WindowWrapper::isDropdownMenu() const
{
    return win::is_dropdown_menu(m_client);
}

bool WindowWrapper::isPopupMenu() const
{
    return win::is_popup_menu(m_client);
}

bool WindowWrapper::isTooltip() const
{
    return win::is_tooltip(m_client);
}

bool WindowWrapper::isNotification() const
{
    return win::is_notification(m_client);
}

bool WindowWrapper::isCriticalNotification() const
{
    return win::is_critical_notification(m_client);
}

bool WindowWrapper::isOnScreenDisplay() const
{
    return win::is_on_screen_display(m_client);
}

bool WindowWrapper::isComboBox() const
{
    return win::is_combo_box(m_client);
}

bool WindowWrapper::isDNDIcon() const
{
    return win::is_dnd_icon(m_client);
}

bool WindowWrapper::isPopupWindow() const
{
    return win::is_popup(m_client);
}

bool WindowWrapper::isSpecialWindow() const
{
    return win::is_special_window(m_client);
}

bool WindowWrapper::isCloseable() const
{
    return m_client->isCloseable();
}

bool WindowWrapper::isMovable() const
{
    return m_client->isMovable();
}

bool WindowWrapper::isMovableAcrossScreens() const
{
    return m_client->isMovableAcrossScreens();
}

bool WindowWrapper::isResizable() const
{
    return m_client->isResizable();
}

bool WindowWrapper::isMinimizable() const
{
    return m_client->isMinimizable();
}

bool WindowWrapper::isMaximizable() const
{
    return m_client->isMaximizable();
}

bool WindowWrapper::isFullscreenable() const
{
    return m_client->isFullScreenable();
}

bool WindowWrapper::isShadeable() const
{
    return m_client->isShadeable();
}

bool WindowWrapper::isOutline() const
{
    return m_client->isOutline();
}

bool WindowWrapper::isShape() const
{
    return m_client->shape();
}

bool WindowWrapper::isShade() const
{
    return m_client->isShade();
}

void WindowWrapper::setShade(bool set)
{
    win::set_shade(m_client, set);
}

bool WindowWrapper::keepAbove() const
{
    return m_client->control()->keep_above();
}

void WindowWrapper::setKeepAbove(bool set)
{
    win::set_keep_above(m_client, set);
}

bool WindowWrapper::keepBelow() const
{
    return m_client->control()->keep_below();
}

void WindowWrapper::setKeepBelow(bool set)
{
    win::set_keep_below(m_client, set);
}

bool WindowWrapper::isMinimized() const
{
    return m_client->control()->minimized();
}

void WindowWrapper::setMinimized(bool set)
{
    win::set_minimized(m_client, set);
}

bool WindowWrapper::skipTaskbar() const
{
    return m_client->control()->skip_taskbar();
}

void WindowWrapper::setSkipTaskbar(bool set)
{
    win::set_skip_taskbar(m_client, set);
}

bool WindowWrapper::skipPager() const
{
    return m_client->control()->skip_pager();
}

void WindowWrapper::setSkipPager(bool set)
{
    win::set_skip_pager(m_client, set);
}

bool WindowWrapper::skipSwitcher() const
{
    return m_client->control()->skip_switcher();
}

void WindowWrapper::setSkipSwitcher(bool set)
{
    win::set_skip_switcher(m_client, set);
}

bool WindowWrapper::skipsCloseAnimation() const
{
    return m_client->skipsCloseAnimation();
}

void WindowWrapper::setSkipCloseAnimation(bool set)
{
    m_client->setSkipCloseAnimation(set);
}

bool WindowWrapper::isActive() const
{
    return m_client->control()->active();
}

bool WindowWrapper::isDemandingAttention() const
{
    return m_client->control()->demands_attention();
}

void WindowWrapper::demandAttention(bool set)
{
    win::set_demands_attention(m_client, set);
}

bool WindowWrapper::wantsInput() const
{
    return m_client->wantsInput();
}

bool WindowWrapper::applicationMenuActive() const
{
    return m_client->control()->application_menu_active();
}

bool WindowWrapper::unresponsive() const
{
    return m_client->control()->unresponsive();
}

bool WindowWrapper::isTransient() const
{
    return m_client->isTransient();
}

WindowWrapper* WindowWrapper::transientFor() const
{
    auto parent = m_client->transientFor();
    if (!parent) {
        return nullptr;
    }
    return m_workspace->get_window(parent);
}

bool WindowWrapper::isModal() const
{
    return m_client->isModal();
}

bool WindowWrapper::decorationHasAlpha() const
{
    return win::decoration_has_alpha(m_client);
}

bool WindowWrapper::hasNoBorder() const
{
    return m_client->noBorder();
}

void WindowWrapper::setNoBorder(bool set)
{
    m_client->setNoBorder(set);
}

QString WindowWrapper::colorScheme() const
{
    return m_client->colorScheme();
}

QByteArray WindowWrapper::desktopFileName() const
{
    return m_client->control()->desktop_file_name();
}

bool WindowWrapper::hasApplicationMenu() const
{
    return m_client->control()->has_application_menu();
}

bool WindowWrapper::providesContextHelp() const
{
    return m_client->providesContextHelp();
}

bool WindowWrapper::isClient() const
{
    return m_client->isClient();
}

bool WindowWrapper::isDeleted() const
{
    return m_client->isDeleted();
}

quint32 WindowWrapper::surfaceId() const
{
    return m_client->surfaceId();
}

Wrapland::Server::Surface* WindowWrapper::surface() const
{
    return m_client->surface();
}

QSize WindowWrapper::basicUnit() const
{
    return m_client->basicUnit();
}

bool WindowWrapper::isBlockingCompositing()
{
    return m_client->isBlockingCompositing();
}

void WindowWrapper::setBlockingCompositing(bool block)
{
    m_client->setBlockingCompositing(block);
}

AbstractClient* WindowWrapper::client() const
{
    return m_client;
}

}
