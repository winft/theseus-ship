/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#include "client_impl.h"

#include "bridge.h"
#include "palette.h"
#include "renderer.h"

#include "base/options.h"
#include "base/platform.h"
#include "input/cursor.h"
#include "render/compositor.h"
#include "render/platform.h"
#include "toplevel.h"
#include "win/actions.h"
#include "win/control.h"
#include "win/dbus/appmenu.h"
#include "win/geo.h"
#include "win/meta.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/user_actions_menu.h"
#include "win/window_operation.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>

#include <QDebug>
#include <QStyle>
#include <QToolTip>

namespace KWin::win::deco
{

client_impl_qobject::~client_impl_qobject() = default;

client_impl::client_impl(Toplevel* window,
                         KDecoration2::DecoratedClient* decoratedClient,
                         KDecoration2::Decoration* decoration)
    : ApplicationMenuEnabledDecoratedClientPrivate(decoratedClient, decoration)
    , qobject{std::make_unique<client_impl_qobject>()}
    , m_client(window)
    , m_clientSize(win::frame_to_client_size(window, window->size()))
    , space{window->space}
{
    createRenderer();
    window->control->deco().set_client(this);

    QObject::connect(window, &Toplevel::activeChanged, qobject.get(), [decoratedClient, window]() {
        Q_EMIT decoratedClient->activeChanged(window->control->active());
    });
    QObject::connect(
        window, &Toplevel::frame_geometry_changed, qobject.get(), [this] { update_size(); });
    QObject::connect(window, &Toplevel::desktopChanged, qobject.get(), [decoratedClient, window]() {
        Q_EMIT decoratedClient->onAllDesktopsChanged(window->isOnAllDesktops());
    });
    QObject::connect(window, &Toplevel::captionChanged, qobject.get(), [decoratedClient, window]() {
        Q_EMIT decoratedClient->captionChanged(win::caption(window));
    });
    QObject::connect(window, &Toplevel::iconChanged, qobject.get(), [decoratedClient, window]() {
        Q_EMIT decoratedClient->iconChanged(window->control->icon());
    });

    QObject::connect(window,
                     &Toplevel::keepAboveChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::keepAboveChanged);
    QObject::connect(window,
                     &Toplevel::keepBelowChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::keepBelowChanged);

    QObject::connect(&space.render,
                     &render::compositor::aboutToToggleCompositing,
                     qobject.get(),
                     [this] { m_renderer.reset(); });
    m_compositorToggledConnection = QObject::connect(&space.render,
                                                     &render::compositor::compositingToggled,
                                                     qobject.get(),
                                                     [this, decoration]() {
                                                         createRenderer();
                                                         decoration->update();
                                                     });
    QObject::connect(&space.render, &render::compositor::aboutToDestroy, qobject.get(), [this] {
        QObject::disconnect(m_compositorToggledConnection);
        m_compositorToggledConnection = QMetaObject::Connection();
    });
    QObject::connect(
        window, &Toplevel::quicktiling_changed, decoratedClient, [this, decoratedClient]() {
            Q_EMIT decoratedClient->adjacentScreenEdgesChanged(adjacentScreenEdges());
        });
    QObject::connect(window,
                     &Toplevel::closeableChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::closeableChanged);
    QObject::connect(window,
                     &Toplevel::minimizeableChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::minimizeableChanged);
    QObject::connect(window,
                     &Toplevel::maximizeableChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::maximizeableChanged);

    QObject::connect(window,
                     &Toplevel::paletteChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::paletteChanged);

    QObject::connect(window,
                     &Toplevel::hasApplicationMenuChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::hasApplicationMenuChanged);
    QObject::connect(window,
                     &Toplevel::applicationMenuActiveChanged,
                     decoratedClient,
                     &KDecoration2::DecoratedClient::applicationMenuActiveChanged);

    m_toolTipWakeUp.setSingleShot(true);
    QObject::connect(&m_toolTipWakeUp, &QTimer::timeout, qobject.get(), [this]() {
        int fallAsleepDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_FallAsleepDelay);
        m_toolTipFallAsleep.setRemainingTime(fallAsleepDelay);

        QToolTip::showText(input::get_cursor()->pos(), m_toolTipText);
        m_toolTipShowing = true;
    });
}

client_impl::~client_impl()
{
    if (m_toolTipShowing) {
        requestHideToolTip();
    }
}

void client_impl::update_size()
{
    if (win::frame_to_client_size(m_client, m_client->size()) == m_clientSize) {
        return;
    }

    auto deco_client = decoratedClient();

    auto const old_size = m_clientSize;
    m_clientSize = win::frame_to_client_size(m_client, m_client->size());

    if (old_size.width() != m_clientSize.width()) {
        Q_EMIT deco_client->widthChanged(m_clientSize.width());
    }
    if (old_size.height() != m_clientSize.height()) {
        Q_EMIT deco_client->heightChanged(m_clientSize.height());
    }
    Q_EMIT deco_client->sizeChanged(m_clientSize);
}

std::unique_ptr<deco::renderer> client_impl::move_renderer()
{
    if (!m_renderer) {
        return {};
    }

    m_renderer->reparent();
    return std::move(m_renderer);
}

QPalette client_impl::palette() const
{
    return m_client->control->palette().q_palette();
}

#define DELEGATE(type, name, clientName)                                                           \
    type client_impl::name() const                                                                 \
    {                                                                                              \
        return m_client->clientName();                                                             \
    }

#define DELEGATE2(type, name) DELEGATE(type, name, name)

DELEGATE2(bool, isCloseable)
DELEGATE(bool, isMaximizeable, isMaximizable)
DELEGATE(bool, isMinimizeable, isMinimizable)
DELEGATE(bool, isMoveable, isMovable)
DELEGATE(bool, isResizeable, isResizable)
DELEGATE2(bool, providesContextHelp)
DELEGATE2(int, desktop)
DELEGATE2(bool, isOnAllDesktops)

#undef DELEGATE2
#undef DELEGATE

#define DELEGATE_WIN(type, name, impl_name)                                                        \
    type client_impl::name() const                                                                 \
    {                                                                                              \
        return win::impl_name(m_client);                                                           \
    }

DELEGATE_WIN(QString, caption, caption)

#undef DELEGATE_WIN

#define DELEGATE_WIN_CTRL(type, name, impl_name)                                                   \
    type client_impl::name() const                                                                 \
    {                                                                                              \
        return m_client->control->impl_name();                                                     \
    }

DELEGATE_WIN_CTRL(bool, isActive, active)
DELEGATE_WIN_CTRL(QIcon, icon, icon)
DELEGATE_WIN_CTRL(bool, isKeepAbove, keep_above)
DELEGATE_WIN_CTRL(bool, isKeepBelow, keep_below)

#undef DELEGATE_WIN_CTRL

#define DELEGATE_WIN_TRANSIENT(type, name, impl_name)                                              \
    type client_impl::name() const                                                                 \
    {                                                                                              \
        return m_client->transient()->impl_name();                                                 \
    }

DELEGATE_WIN_TRANSIENT(bool, isModal, modal)

#undef DELEGATE_WIN_TRANSIENT

#define DELEGATE(type, name, clientName)                                                           \
    type client_impl::name() const                                                                 \
    {                                                                                              \
        return m_client->clientName();                                                             \
    }

DELEGATE(WId, decorationId, frameId)

#undef DELEGATE

#define DELEGATE(name, op)                                                                         \
    void client_impl::name()                                                                       \
    {                                                                                              \
        win::perform_window_operation(space, m_client, base::options::op);                         \
    }

DELEGATE(requestToggleOnAllDesktops, OnAllDesktopsOp)
DELEGATE(requestToggleKeepAbove, KeepAboveOp)
DELEGATE(requestToggleKeepBelow, KeepBelowOp)

#undef DELEGATE

#define DELEGATE(name, clientName)                                                                 \
    void client_impl::name()                                                                       \
    {                                                                                              \
        m_client->clientName();                                                                    \
    }

DELEGATE(requestContextHelp, showContextHelp)

#undef DELEGATE

WId client_impl::windowId() const
{
    return m_client->xcb_window;
}

void client_impl::requestMinimize()
{
    win::set_minimized(m_client, true);
}

void client_impl::requestClose()
{
    QMetaObject::invokeMethod(m_client, "closeWindow", Qt::QueuedConnection);
}

QColor client_impl::color(KDecoration2::ColorGroup group, KDecoration2::ColorRole role) const
{
    auto dp = m_client->control->palette().current;
    if (dp) {
        return dp->color(group, role);
    }

    return QColor();
}

void client_impl::requestShowToolTip(const QString& text)
{
    if (!space.deco->showToolTips()) {
        return;
    }

    m_toolTipText = text;

    int wakeUpDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay);
    m_toolTipWakeUp.start(m_toolTipFallAsleep.hasExpired() ? wakeUpDelay : 20);
}

void client_impl::requestHideToolTip()
{
    m_toolTipWakeUp.stop();
    QToolTip::hideText();
    m_toolTipShowing = false;
}

void client_impl::requestShowWindowMenu(QRect const& rect)
{
    // TODO: add rect to requestShowWindowMenu
    auto const client_pos = m_client->pos();
    space.user_actions_menu->show(
        QRect(client_pos + rect.topLeft(), client_pos + rect.bottomRight()), m_client);
}

void client_impl::requestShowApplicationMenu(const QRect& rect, int actionId)
{
    space.appmenu->showApplicationMenu(m_client->pos() + rect.bottomLeft(), m_client, actionId);
}

void client_impl::showApplicationMenu(int actionId)
{
    decoration()->showApplicationMenu(actionId);
}

void client_impl::requestToggleMaximization(Qt::MouseButtons buttons)
{
    QMetaObject::invokeMethod(
        qobject.get(),
        [this, buttons] {
            perform_window_operation(
                space, m_client, kwinApp()->options->operationMaxButtonClick(buttons));
        },
        Qt::QueuedConnection);
}

int client_impl::width() const
{
    return m_clientSize.width();
}

int client_impl::height() const
{
    return m_clientSize.height();
}

QSize client_impl::size() const
{
    return m_clientSize;
}

bool client_impl::isMaximizedVertically() const
{
    return flags(m_client->maximizeMode() & win::maximize_mode::vertical);
}

bool client_impl::isMaximized() const
{
    return isMaximizedHorizontally() && isMaximizedVertically();
}

bool client_impl::isMaximizedHorizontally() const
{
    return flags(m_client->maximizeMode() & win::maximize_mode::horizontal);
}

Qt::Edges client_impl::adjacentScreenEdges() const
{
    Qt::Edges edges;
    auto const mode = m_client->control->quicktiling();
    if (flags(mode & win::quicktiles::left)) {
        edges |= Qt::LeftEdge;
        if (!flags(mode & (win::quicktiles::top | win::quicktiles::bottom))) {
            // using complete side
            edges |= Qt::TopEdge | Qt::BottomEdge;
        }
    }
    if (flags(mode & win::quicktiles::top)) {
        edges |= Qt::TopEdge;
    }
    if (flags(mode & win::quicktiles::right)) {
        edges |= Qt::RightEdge;
        if (!flags(mode & (win::quicktiles::top | win::quicktiles::bottom))) {
            // using complete side
            edges |= Qt::TopEdge | Qt::BottomEdge;
        }
    }
    if (flags(mode & win::quicktiles::bottom)) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

bool client_impl::hasApplicationMenu() const
{
    return m_client->control->has_application_menu();
}

bool client_impl::isApplicationMenuActive() const
{
    return m_client->control->application_menu_active();
}

void client_impl::createRenderer()
{
    m_renderer.reset(kwinApp()->get_base().render->createDecorationRenderer(this));
}

}
