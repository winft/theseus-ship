/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "space.h"

#include "toplevel.h"
#include "win/controlling.h"
#include "win/meta.h"
#include "win/screen.h"
#include "win/transient.h"
#include "win/x11/window.h"

namespace KWin::scripting
{

window::window(Toplevel* client, space* workspace)
    : m_client{client}
    , m_workspace{workspace}
{
    connect(client,
            &Toplevel::opacityChanged,
            this,
            [this]([[maybe_unused]] auto toplevel, auto oldOpacity) {
                Q_EMIT opacityChanged(this, oldOpacity);
            });

    connect(client, &Toplevel::activeChanged, this, &window::activeChanged);
    connect(client, &Toplevel::demandsAttentionChanged, this, &window::demandsAttentionChanged);
    connect(client,
            &Toplevel::desktopPresenceChanged,
            this,
            [this]([[maybe_unused]] auto toplevel, auto desktop) {
                Q_EMIT desktopPresenceChanged(this, desktop);
            });
    connect(client, &Toplevel::desktopChanged, this, &window::desktopChanged);
    connect(client, &Toplevel::x11DesktopIdsChanged, this, &window::x11DesktopIdsChanged);

    connect(client, &Toplevel::minimizedChanged, this, &window::minimizedChanged);
    connect(client, &Toplevel::clientMinimized, this, [this] { Q_EMIT clientMinimized(this); });
    connect(client, &Toplevel::clientUnminimized, this, [this] { Q_EMIT clientUnminimized(this); });

    connect(client,
            qOverload<Toplevel*, bool, bool>(&Toplevel::clientMaximizedStateChanged),
            this,
            [this]([[maybe_unused]] Toplevel* client, bool horizontal, bool vertical) {
                Q_EMIT clientMaximizedStateChanged(this, horizontal, vertical);
            });

    connect(client, &Toplevel::quicktiling_changed, this, &window::quickTileModeChanged);

    connect(client, &Toplevel::keepAboveChanged, this, &window::keepAboveChanged);
    connect(client, &Toplevel::keepBelowChanged, this, &window::keepBelowChanged);

    connect(client, &Toplevel::fullScreenChanged, this, &window::fullScreenChanged);
    connect(client, &Toplevel::skipTaskbarChanged, this, &window::skipTaskbarChanged);
    connect(client, &Toplevel::skipPagerChanged, this, &window::skipPagerChanged);
    connect(client, &Toplevel::skipSwitcherChanged, this, &window::skipSwitcherChanged);

    connect(client, &Toplevel::paletteChanged, this, &window::paletteChanged);
    connect(client, &Toplevel::colorSchemeChanged, this, &window::colorSchemeChanged);
    connect(client, &Toplevel::transientChanged, this, &window::transientChanged);
    connect(client, &Toplevel::modalChanged, this, &window::modalChanged);

    connect(client, &Toplevel::moveResizedChanged, this, &window::moveResizedChanged);
    connect(client, &Toplevel::moveResizeCursorChanged, this, &window::moveResizeCursorChanged);
    connect(client, &Toplevel::clientStartUserMovedResized, this, [this] {
        Q_EMIT clientStartUserMovedResized(this);
    });
    connect(client,
            &Toplevel::clientStepUserMovedResized,
            this,
            [this]([[maybe_unused]] auto toplevel, auto rect) {
                Q_EMIT clientStepUserMovedResized(this, rect);
            });
    connect(client, &Toplevel::clientFinishUserMovedResized, this, [this] {
        Q_EMIT clientFinishUserMovedResized(this);
    });

    connect(client, &Toplevel::windowClassChanged, this, &window::windowClassChanged);
    connect(client, &Toplevel::captionChanged, this, &window::captionChanged);
    connect(client, &Toplevel::iconChanged, this, &window::iconChanged);
    connect(client, &Toplevel::frame_geometry_changed, this, &window::geometryChanged);
    connect(client, &Toplevel::hasAlphaChanged, this, &window::hasAlphaChanged);
    connect(client, &Toplevel::central_output_changed, this, &window::screenChanged);
    connect(client, &Toplevel::windowRoleChanged, this, &window::windowRoleChanged);
    connect(client, &Toplevel::shapedChanged, this, &window::shapedChanged);
    connect(client, &Toplevel::skipCloseAnimationChanged, this, &window::skipCloseAnimationChanged);
    connect(client,
            &Toplevel::applicationMenuActiveChanged,
            this,
            &window::applicationMenuActiveChanged);
    connect(client, &Toplevel::unresponsiveChanged, this, &window::unresponsiveChanged);
    connect(client, &Toplevel::hasApplicationMenuChanged, this, &window::hasApplicationMenuChanged);
    connect(client, &Toplevel::surfaceIdChanged, this, &window::surfaceIdChanged);

    connect(client, &Toplevel::closeableChanged, this, &window::closeableChanged);
    connect(client, &Toplevel::minimizeableChanged, this, &window::minimizeableChanged);
    connect(client, &Toplevel::maximizeableChanged, this, &window::maximizeableChanged);

    connect(client, &Toplevel::desktopFileNameChanged, this, &window::desktopFileNameChanged);

    if (client->isClient()) {
        auto x11_client = dynamic_cast<win::x11::window*>(m_client);
        connect(x11_client,
                &win::x11::window::client_fullscreen_set,
                this,
                [this]([[maybe_unused]] auto client, bool fullscreen, bool user) {
                    Q_EMIT clientFullScreenSet(this, fullscreen, user);
                });
        connect(client, &Toplevel::blockingCompositingChanged, this, [this] {
            Q_EMIT blockingCompositingChanged(this);
        });
    }
}

xcb_window_t window::frameId() const
{
    return m_client->frameId();
}

quint32 window::windowId() const
{
    return m_client->xcb_window();
}

QByteArray window::resourceName() const
{
    return m_client->resourceName();
}

QByteArray window::resourceClass() const
{
    return m_client->resourceClass();
}

QString window::caption() const
{
    return win::caption(m_client);
}

QIcon window::icon() const
{
    return m_client->control->icon();
}

QRect window::iconGeometry() const
{
    return m_client->iconGeometry();
}

QUuid window::internalId() const
{
    return m_client->internalId();
}

pid_t window::pid() const
{
    return m_client->pid();
}

QRect window::bufferGeometry() const
{
    return win::render_geometry(m_client);
}

QRect window::frameGeometry() const
{
    return m_client->frameGeometry();
}

void window::setFrameGeometry(QRect const& geo)
{
    m_client->setFrameGeometry(geo);
}

QPoint window::pos() const
{
    return m_client->pos();
}

QRect window::rect() const
{
    return QRect(QPoint(0, 0), m_client->size());
}

QRect window::visibleRect() const
{
    return win::visible_rect(m_client);
}

QSize window::size() const
{
    return m_client->size();
}

QSize window::minSize() const
{
    return m_client->minSize();
}

QSize window::maxSize() const
{
    return m_client->maxSize();
}

QPoint window::clientPos() const
{
    return win::frame_relative_client_rect(m_client).topLeft();
}

QSize window::clientSize() const
{
    return win::frame_to_client_size(m_client, m_client->size());
}

int window::x() const
{
    return m_client->pos().x();
}

int window::y() const
{
    return m_client->pos().y();
}

int window::width() const
{
    return m_client->size().width();
}

int window::height() const
{
    return m_client->size().height();
}

bool window::isMove() const
{
    return win::is_move(m_client);
}

bool window::isResize() const
{
    return win::is_resize(m_client);
}

bool window::hasAlpha() const
{
    return m_client->hasAlpha();
}

qreal window::opacity() const
{
    return m_client->opacity();
}

void window::setOpacity(qreal opacity)
{
    m_client->setOpacity(opacity);
}

bool window::isFullScreen() const
{
    return m_client->control->fullscreen();
}

void window::setFullScreen(bool set)
{
    m_client->setFullScreen(set);
}

int window::screen() const
{
    return base::get_output_index(kwinApp()->get_base().get_outputs(), m_client->central_output);
}

int window::desktop() const
{
    return m_client->desktop();
}

void window::setDesktop(int desktop)
{
    win::set_desktop(m_client, desktop);
}

QVector<uint> window::x11DesktopIds() const
{
    return win::x11_desktop_ids(m_client);
}

bool window::isOnAllDesktops() const
{
    return m_client->isOnAllDesktops();
}

void window::setOnAllDesktops(bool set)
{
    win::set_on_all_desktops(m_client, set);
}

QStringList window::activities() const
{
    return {};
}

QByteArray window::windowRole() const
{
    return m_client->windowRole();
}

NET::WindowType window::windowType(bool direct, int supported_types) const
{
    return m_client->windowType(direct, supported_types);
}

bool window::isDesktop() const
{
    return win::is_desktop(m_client);
}

bool window::isDock() const
{
    return win::is_dock(m_client);
}

bool window::isToolbar() const
{
    return win::is_toolbar(m_client);
}

bool window::isMenu() const
{
    return win::is_menu(m_client);
}

bool window::isNormalWindow() const
{
    return win::is_normal(m_client);
}

bool window::isDialog() const
{
    return win::is_dialog(m_client);
}

bool window::isSplash() const
{
    return win::is_splash(m_client);
}

bool window::isUtility() const
{
    return win::is_utility(m_client);
}

bool window::isDropdownMenu() const
{
    return win::is_dropdown_menu(m_client);
}

bool window::isPopupMenu() const
{
    return win::is_popup_menu(m_client);
}

bool window::isTooltip() const
{
    return win::is_tooltip(m_client);
}

bool window::isNotification() const
{
    return win::is_notification(m_client);
}

bool window::isCriticalNotification() const
{
    return win::is_critical_notification(m_client);
}

bool window::isOnScreenDisplay() const
{
    return win::is_on_screen_display(m_client);
}

bool window::isComboBox() const
{
    return win::is_combo_box(m_client);
}

bool window::isDNDIcon() const
{
    return win::is_dnd_icon(m_client);
}

bool window::isPopupWindow() const
{
    return win::is_popup(m_client);
}

bool window::isSpecialWindow() const
{
    return win::is_special_window(m_client);
}

bool window::isCloseable() const
{
    return m_client->isCloseable();
}

bool window::isMovable() const
{
    return m_client->isMovable();
}

bool window::isMovableAcrossScreens() const
{
    return m_client->isMovableAcrossScreens();
}

bool window::isResizable() const
{
    return m_client->isResizable();
}

bool window::isMinimizable() const
{
    return m_client->isMinimizable();
}

bool window::isMaximizable() const
{
    return m_client->isMaximizable();
}

bool window::isFullScreenable() const
{
    return m_client->control->can_fullscreen();
}

bool window::isShadeable() const
{
    return false;
}

bool window::isOutline() const
{
    return m_client->isOutline();
}

bool window::isShape() const
{
    return m_client->shape();
}

bool window::isShade() const
{
    return false;
}

void window::setShade([[maybe_unused]] bool set)
{
}

bool window::keepAbove() const
{
    return m_client->control->keep_above();
}

void window::setKeepAbove(bool set)
{
    win::set_keep_above(m_client, set);
}

bool window::keepBelow() const
{
    return m_client->control->keep_below();
}

void window::setKeepBelow(bool set)
{
    win::set_keep_below(m_client, set);
}

bool window::isMinimized() const
{
    return m_client->control->minimized();
}

void window::setMinimized(bool set)
{
    win::set_minimized(m_client, set);
}

bool window::skipTaskbar() const
{
    return m_client->control->skip_taskbar();
}

void window::setSkipTaskbar(bool set)
{
    win::set_skip_taskbar(m_client, set);
}

bool window::skipPager() const
{
    return m_client->control->skip_pager();
}

void window::setSkipPager(bool set)
{
    win::set_skip_pager(m_client, set);
}

bool window::skipSwitcher() const
{
    return m_client->control->skip_switcher();
}

void window::setSkipSwitcher(bool set)
{
    win::set_skip_switcher(m_client, set);
}

bool window::skipsCloseAnimation() const
{
    return m_client->skipsCloseAnimation();
}

void window::setSkipCloseAnimation(bool set)
{
    m_client->setSkipCloseAnimation(set);
}

bool window::isActive() const
{
    return m_client->control->active();
}

bool window::isDemandingAttention() const
{
    return m_client->control->demands_attention();
}

void window::demandAttention(bool set)
{
    win::set_demands_attention(m_client, set);
}

bool window::wantsInput() const
{
    return m_client->wantsInput();
}

bool window::applicationMenuActive() const
{
    return m_client->control->application_menu_active();
}

bool window::unresponsive() const
{
    return m_client->control->unresponsive();
}

bool window::isTransient() const
{
    return m_client->transient()->lead();
}

window* window::transientFor() const
{
    auto parent = m_client->transient()->lead();
    if (!parent) {
        return nullptr;
    }
    return m_workspace->get_window(parent);
}

bool window::isModal() const
{
    return m_client->transient()->modal();
}

bool window::decorationHasAlpha() const
{
    return win::decoration_has_alpha(m_client);
}

bool window::hasNoBorder() const
{
    return m_client->noBorder();
}

void window::setNoBorder(bool set)
{
    m_client->setNoBorder(set);
}

QString window::colorScheme() const
{
    return m_client->control->palette().color_scheme;
}

QByteArray window::desktopFileName() const
{
    return m_client->control->desktop_file_name();
}

bool window::hasApplicationMenu() const
{
    return m_client->control->has_application_menu();
}

bool window::providesContextHelp() const
{
    return m_client->providesContextHelp();
}

bool window::isClient() const
{
    return m_client->isClient();
}

bool window::isDeleted() const
{
    return m_client->isDeleted();
}

quint32 window::surfaceId() const
{
    return m_client->surfaceId();
}

Wrapland::Server::Surface* window::surface() const
{
    return m_client->surface();
}

QSize window::basicUnit() const
{
    return m_client->basicUnit();
}

bool window::isBlockingCompositing()
{
    return m_client->isBlockingCompositing();
}

void window::setBlockingCompositing(bool block)
{
    m_client->setBlockingCompositing(block);
}

Toplevel* window::client() const
{
    return m_client;
}

}
