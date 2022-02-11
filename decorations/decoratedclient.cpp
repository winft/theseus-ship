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
#include "decoratedclient.h"
#include "decorationbridge.h"
#include "decorationpalette.h"
#include "decorationrenderer.h"

#include "base/platform.h"
#include "render/compositor.h"
#include "input/cursor.h"
#include "options.h"
#include "render/platform.h"
#include "toplevel.h"
#include "win/control.h"
#include "win/geo.h"
#include "win/meta.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/transient.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>

#include <QDebug>
#include <QStyle>
#include <QToolTip>

namespace KWin
{
namespace Decoration
{

DecoratedClientImpl::DecoratedClientImpl(Toplevel* window,
                                         KDecoration2::DecoratedClient *decoratedClient,
                                         KDecoration2::Decoration *decoration)
    : QObject()
    , ApplicationMenuEnabledDecoratedClientPrivate(decoratedClient, decoration)
    , m_client(window)
    , m_clientSize(win::frame_to_client_size(window, window->size()))
    , m_renderer(nullptr)
{
    createRenderer();
    window->control->deco().set_client(this);

    connect(window, &Toplevel::activeChanged, this,
        [decoratedClient, window]() {
            Q_EMIT decoratedClient->activeChanged(window->control->active());
        }
    );
    connect(window, &Toplevel::frame_geometry_changed, this, &DecoratedClientImpl::update_size);
    connect(window, &Toplevel::desktopChanged, this,
        [decoratedClient, window]() {
            Q_EMIT decoratedClient->onAllDesktopsChanged(window->isOnAllDesktops());
        }
    );
    connect(window, &Toplevel::captionChanged, this,
        [decoratedClient, window]() {
            Q_EMIT decoratedClient->captionChanged(win::caption(window));
        }
    );
    connect(window, &Toplevel::iconChanged, this,
        [decoratedClient, window]() {
            Q_EMIT decoratedClient->iconChanged(window->control->icon());
        }
    );

    connect(window, &Toplevel::keepAboveChanged,
            decoratedClient, &KDecoration2::DecoratedClient::keepAboveChanged);
    connect(window, &Toplevel::keepBelowChanged,
            decoratedClient, &KDecoration2::DecoratedClient::keepBelowChanged);

    connect(render::compositor::self(), &render::compositor::aboutToToggleCompositing, this, &DecoratedClientImpl::destroyRenderer);
    m_compositorToggledConnection = connect(render::compositor::self(), &render::compositor::compositingToggled, this,
        [this, decoration]() {
            createRenderer();
            decoration->update();
        }
    );
    connect(render::compositor::self(), &render::compositor::aboutToDestroy, this,
        [this] {
            disconnect(m_compositorToggledConnection);
            m_compositorToggledConnection = QMetaObject::Connection();
        }
    );
    connect(window, &Toplevel::quicktiling_changed, decoratedClient,
        [this, decoratedClient]() {
            Q_EMIT decoratedClient->adjacentScreenEdgesChanged(adjacentScreenEdges());
        }
    );
    connect(window, &Toplevel::closeableChanged,
            decoratedClient, &KDecoration2::DecoratedClient::closeableChanged);
    connect(window, &Toplevel::minimizeableChanged,
            decoratedClient, &KDecoration2::DecoratedClient::minimizeableChanged);
    connect(window, &Toplevel::maximizeableChanged,
            decoratedClient, &KDecoration2::DecoratedClient::maximizeableChanged);

    connect(window, &Toplevel::paletteChanged,
            decoratedClient, &KDecoration2::DecoratedClient::paletteChanged);

    connect(window, &Toplevel::hasApplicationMenuChanged,
            decoratedClient, &KDecoration2::DecoratedClient::hasApplicationMenuChanged);
    connect(window, &Toplevel::applicationMenuActiveChanged,
            decoratedClient, &KDecoration2::DecoratedClient::applicationMenuActiveChanged);

    m_toolTipWakeUp.setSingleShot(true);
    connect(&m_toolTipWakeUp, &QTimer::timeout, this,
            [this]() {
                int fallAsleepDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_FallAsleepDelay);
                this->m_toolTipFallAsleep.setRemainingTime(fallAsleepDelay);

                QToolTip::showText(input::get_cursor()->pos(), this->m_toolTipText);
                m_toolTipShowing = true;
            }
    );
}

DecoratedClientImpl::~DecoratedClientImpl()
{
    if (m_toolTipShowing) {
        requestHideToolTip();
    }
}

void DecoratedClientImpl::update_size()
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

QPalette DecoratedClientImpl::palette() const
{
    return m_client->control->palette().q_palette();
}

#define DELEGATE(type, name, clientName) \
    type DecoratedClientImpl::name() const \
    { \
        return m_client->clientName(); \
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

#define DELEGATE_WIN(type, name, impl_name) \
    type DecoratedClientImpl::name() const \
    { \
        return win::impl_name(m_client); \
    }

DELEGATE_WIN(QString, caption, caption)

#undef DELEGATE_WIN

#define DELEGATE_WIN_CTRL(type, name, impl_name) \
    type DecoratedClientImpl::name() const \
    { \
        return m_client->control->impl_name(); \
    }

DELEGATE_WIN_CTRL(bool, isActive, active)
DELEGATE_WIN_CTRL(QIcon, icon, icon)
DELEGATE_WIN_CTRL(bool, isKeepAbove, keep_above)
DELEGATE_WIN_CTRL(bool, isKeepBelow, keep_below)

#undef DELEGATE_WIN_CTRL

#define DELEGATE_WIN_TRANSIENT(type, name, impl_name) \
    type DecoratedClientImpl::name() const \
    { \
        return m_client->transient()->impl_name(); \
    }

DELEGATE_WIN_TRANSIENT(bool, isModal, modal)

#undef DELEGATE_WIN_TRANSIENT

#define DELEGATE(type, name, clientName) \
    type DecoratedClientImpl::name() const \
    { \
        return m_client->clientName(); \
    }

DELEGATE(WId, windowId, xcb_window)
DELEGATE(WId, decorationId, frameId)

#undef DELEGATE

#define DELEGATE(name, op) \
    void DecoratedClientImpl::name() \
    { \
        workspace()->performWindowOperation(m_client, Options::op); \
    }

DELEGATE(requestToggleOnAllDesktops, OnAllDesktopsOp)
DELEGATE(requestToggleKeepAbove, KeepAboveOp)
DELEGATE(requestToggleKeepBelow, KeepBelowOp)

#undef DELEGATE

#define DELEGATE(name, clientName) \
    void DecoratedClientImpl::name() \
    { \
        m_client->clientName(); \
    }

DELEGATE(requestContextHelp, showContextHelp)

#undef DELEGATE

void DecoratedClientImpl::requestMinimize()
{
    win::set_minimized(m_client, true);
}

void DecoratedClientImpl::requestClose()
{
    QMetaObject::invokeMethod(m_client, "closeWindow", Qt::QueuedConnection);
}

QColor DecoratedClientImpl::color(KDecoration2::ColorGroup group, KDecoration2::ColorRole role) const
{
    auto dp = m_client->control->palette().current;
    if (dp) {
        return dp->color(group, role);
    }

    return QColor();
}

void DecoratedClientImpl::requestShowToolTip(const QString &text)
{
    if (!DecorationBridge::self()->showToolTips()) {
        return;
    }

    m_toolTipText = text;

    int wakeUpDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay);
    m_toolTipWakeUp.start(m_toolTipFallAsleep.hasExpired() ? wakeUpDelay : 20);
}

void DecoratedClientImpl::requestHideToolTip()
{
    m_toolTipWakeUp.stop();
    QToolTip::hideText();
    m_toolTipShowing = false;
}

void DecoratedClientImpl::requestShowWindowMenu(QRect const& rect)
{
    // TODO: add rect to requestShowWindowMenu
    auto const client_pos = m_client->pos();
    workspace()->showWindowMenu(QRect(client_pos + rect.topLeft(),
                                            client_pos + rect.bottomRight()),
                                      m_client);
}

void DecoratedClientImpl::requestShowApplicationMenu(const QRect &rect, int actionId)
{
    workspace()->showApplicationMenu(rect, m_client, actionId);
}

void DecoratedClientImpl::showApplicationMenu(int actionId)
{
    decoration()->showApplicationMenu(actionId);
}

void DecoratedClientImpl::requestToggleMaximization(Qt::MouseButtons buttons)
{
    QMetaObject::invokeMethod(
        this,
        "delayedRequestToggleMaximization",
        Qt::QueuedConnection,
        Q_ARG(Options::WindowOperation, kwinApp()->options->operationMaxButtonClick(buttons)));
}

void DecoratedClientImpl::delayedRequestToggleMaximization(Options::WindowOperation operation)
{
    workspace()->performWindowOperation(m_client, operation);
}

int DecoratedClientImpl::width() const
{
    return m_clientSize.width();
}

int DecoratedClientImpl::height() const
{
    return m_clientSize.height();
}

QSize DecoratedClientImpl::size() const
{
    return m_clientSize;
}

bool DecoratedClientImpl::isMaximizedVertically() const
{
    return flags(m_client->maximizeMode() & win::maximize_mode::vertical);
}

bool DecoratedClientImpl::isMaximized() const
{
    return isMaximizedHorizontally() && isMaximizedVertically();
}

bool DecoratedClientImpl::isMaximizedHorizontally() const
{
    return flags(m_client->maximizeMode() & win::maximize_mode::horizontal);
}

Qt::Edges DecoratedClientImpl::adjacentScreenEdges() const
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

bool DecoratedClientImpl::hasApplicationMenu() const
{
    return m_client->control->has_application_menu();
}

bool DecoratedClientImpl::isApplicationMenuActive() const
{
    return m_client->control->application_menu_active();
}

void DecoratedClientImpl::createRenderer()
{
    m_renderer = kwinApp()->get_base().render->createDecorationRenderer(this);
}

void DecoratedClientImpl::destroyRenderer()
{
    delete m_renderer;
    m_renderer = nullptr;
}

}
}
