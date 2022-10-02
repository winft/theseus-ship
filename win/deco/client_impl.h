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
#pragma once

#include "client_impl_qobject.h"
#include "renderer.h"

#include "base/options.h"
#include "render/compositor_qobject.h"
#include "render/platform.h"
#include "win/geo.h"
#include "win/meta.h"
#include "win/window_operation.h"
#include "win/window_qobject.h"

#include <KDecoration2/Private/DecoratedClientPrivate>
#include <QDeadlineTimer>
#include <QObject>
#include <QStyle>
#include <QTimer>
#include <QToolTip>

namespace KWin::win::deco
{

template<typename Window>
class client_impl : public KDecoration2::ApplicationMenuEnabledDecoratedClientPrivate
{
public:
    using renderer_t = deco::renderer<client_impl>;

    client_impl(Window* window,
                KDecoration2::DecoratedClient* decoratedClient,
                KDecoration2::Decoration* decoration)
        : ApplicationMenuEnabledDecoratedClientPrivate(decoratedClient, decoration)
        , qobject{std::make_unique<client_impl_qobject>()}
        , m_client(window)
        , m_clientSize(win::frame_to_client_size(window, window->geo.size()))
    {
        createRenderer();
        window->control->deco.set_client(this);

        QObject::connect(window->qobject.get(),
                         &window_qobject::activeChanged,
                         qobject.get(),
                         [decoratedClient, window]() {
                             Q_EMIT decoratedClient->activeChanged(window->control->active);
                         });
        QObject::connect(window->qobject.get(),
                         &window_qobject::frame_geometry_changed,
                         qobject.get(),
                         [this] { update_size(); });
        QObject::connect(window->qobject.get(),
                         &window_qobject::desktopChanged,
                         qobject.get(),
                         [decoratedClient, window]() {
                             Q_EMIT decoratedClient->onAllDesktopsChanged(on_all_desktops(window));
                         });
        QObject::connect(window->qobject.get(),
                         &window_qobject::captionChanged,
                         qobject.get(),
                         [decoratedClient, window]() {
                             Q_EMIT decoratedClient->captionChanged(win::caption(window));
                         });
        QObject::connect(window->qobject.get(),
                         &window_qobject::iconChanged,
                         qobject.get(),
                         [decoratedClient, window]() {
                             Q_EMIT decoratedClient->iconChanged(window->control->icon);
                         });

        QObject::connect(window->qobject.get(),
                         &window_qobject::keepAboveChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::keepAboveChanged);
        QObject::connect(window->qobject.get(),
                         &window_qobject::keepBelowChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::keepBelowChanged);

        QObject::connect(m_client->space.base.render->compositor->qobject.get(),
                         &render::compositor_qobject::aboutToToggleCompositing,
                         qobject.get(),
                         [this] { m_renderer.reset(); });
        m_compositorToggledConnection
            = QObject::connect(m_client->space.base.render->compositor->qobject.get(),
                               &render::compositor_qobject::compositingToggled,
                               qobject.get(),
                               [this](auto active) { handle_compositing_toggled(active); });
        QObject::connect(m_client->space.base.render->compositor->qobject.get(),
                         &render::compositor_qobject::aboutToDestroy,
                         qobject.get(),
                         [this] {
                             QObject::disconnect(m_compositorToggledConnection);
                             m_compositorToggledConnection = QMetaObject::Connection();
                         });
        QObject::connect(window->qobject.get(),
                         &window_qobject::quicktiling_changed,
                         decoratedClient,
                         [this, decoratedClient]() {
                             Q_EMIT decoratedClient->adjacentScreenEdgesChanged(
                                 adjacentScreenEdges());
                         });
        QObject::connect(window->qobject.get(),
                         &window_qobject::closeableChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::closeableChanged);
        QObject::connect(window->qobject.get(),
                         &window_qobject::minimizeableChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::minimizeableChanged);
        QObject::connect(window->qobject.get(),
                         &window_qobject::maximizeableChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::maximizeableChanged);

        QObject::connect(window->qobject.get(),
                         &window_qobject::paletteChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::paletteChanged);

        QObject::connect(window->qobject.get(),
                         &window_qobject::hasApplicationMenuChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::hasApplicationMenuChanged);
        QObject::connect(window->qobject.get(),
                         &window_qobject::applicationMenuActiveChanged,
                         decoratedClient,
                         &KDecoration2::DecoratedClient::applicationMenuActiveChanged);

        m_toolTipWakeUp.setSingleShot(true);
        QObject::connect(&m_toolTipWakeUp, &QTimer::timeout, qobject.get(), [this]() {
            int fallAsleepDelay
                = QApplication::style()->styleHint(QStyle::SH_ToolTip_FallAsleepDelay);
            m_toolTipFallAsleep.setRemainingTime(fallAsleepDelay);

            QToolTip::showText(m_client->space.input->cursor->pos(), m_toolTipText);
            m_toolTipShowing = true;
        });
    }

    ~client_impl() override
    {
        if (m_toolTipShowing) {
            requestHideToolTip();
        }
    }

    QString caption() const override
    {
        return win::caption(m_client);
    }

    WId decorationId() const override
    {
        return m_client->frameId();
    }

    int desktop() const override
    {
        return get_desktop(*m_client);
    }

    int height() const override
    {
        return m_clientSize.height();
    }

    QIcon icon() const override
    {
        return m_client->control->icon;
    }

    bool isActive() const override
    {
        return m_client->control->active;
    }

    bool isCloseable() const override
    {
        return m_client->isCloseable();
    }

    bool isKeepAbove() const override
    {
        return m_client->control->keep_above;
    }

    bool isKeepBelow() const override
    {
        return m_client->control->keep_below;
    }

    bool isMaximizeable() const override
    {
        return m_client->isMaximizable();
    }

    bool isMaximized() const override
    {
        return isMaximizedHorizontally() && isMaximizedVertically();
    }

    bool isMaximizedHorizontally() const override
    {
        return flags(m_client->maximizeMode() & win::maximize_mode::horizontal);
    }

    bool isMaximizedVertically() const override
    {
        return flags(m_client->maximizeMode() & win::maximize_mode::vertical);
    }

    bool isMinimizeable() const override
    {
        return m_client->isMinimizable();
    }

    bool isModal() const override
    {
        return m_client->transient->modal();
    }

    bool isMoveable() const override
    {
        return m_client->isMovable();
    }

    bool isOnAllDesktops() const override
    {
        return on_all_desktops(m_client);
    }

    bool isResizeable() const override
    {
        return m_client->isResizable();
    }

    // Deprecated.
    bool isShadeable() const override
    {
        return false;
    }
    bool isShaded() const override
    {
        return false;
    }

    QPalette palette() const override
    {
        return m_client->control->palette.q_palette();
    }

    QColor color(KDecoration2::ColorGroup group, KDecoration2::ColorRole role) const override
    {
        auto dp = m_client->control->palette.current;
        if (dp) {
            return dp->color(group, role);
        }

        return QColor();
    }

    bool providesContextHelp() const override
    {
        return m_client->providesContextHelp();
    }

    QSize size() const override
    {
        return m_clientSize;
    }

    int width() const override
    {
        return m_clientSize.width();
    }

    WId windowId() const override
    {
        return m_client->xcb_window;
    }

    Qt::Edges adjacentScreenEdges() const override
    {
        Qt::Edges edges;
        auto const mode = m_client->control->quicktiling;
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

    bool hasApplicationMenu() const override
    {
        return m_client->control->has_application_menu();
    }

    bool isApplicationMenuActive() const override
    {
        return m_client->control->appmenu.active;
    }

    void requestShowToolTip(const QString& text) override
    {
        if (!m_client->space.deco->showToolTips()) {
            return;
        }

        m_toolTipText = text;

        int wakeUpDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay);
        m_toolTipWakeUp.start(m_toolTipFallAsleep.hasExpired() ? wakeUpDelay : 20);
    }

    void requestHideToolTip() override
    {
        m_toolTipWakeUp.stop();
        QToolTip::hideText();
        m_toolTipShowing = false;
    }

    void requestClose() override
    {
        QMetaObject::invokeMethod(
            m_client->qobject.get(),
            [win = m_client] { win->closeWindow(); },
            Qt::QueuedConnection);
    }

    void requestContextHelp() override
    {
        m_client->showContextHelp();
    }

    void requestToggleMaximization(Qt::MouseButtons buttons) override
    {
        QMetaObject::invokeMethod(
            qobject.get(),
            [this, buttons] {
                perform_window_operation(
                    m_client, kwinApp()->options->qobject->operationMaxButtonClick(buttons));
            },
            Qt::QueuedConnection);
    }

    void requestMinimize() override
    {
        win::set_minimized(m_client, true);
    }

    void requestShowWindowMenu(QRect const& rect) override
    {
        // TODO: add rect to requestShowWindowMenu
        auto const client_pos = m_client->geo.pos();
        m_client->space.user_actions_menu->show(
            QRect(client_pos + rect.topLeft(), client_pos + rect.bottomRight()), m_client);
    }

    void requestShowApplicationMenu(const QRect& rect, int actionId) override
    {
        if (m_client->control->has_application_menu()) {
            m_client->space.appmenu->showApplicationMenu(
                m_client->geo.pos() + rect.bottomLeft(), m_client->control->appmenu, actionId);
        }
    }

    void requestToggleKeepAbove() override
    {
        perform_window_operation(m_client, base::options_qobject::KeepAboveOp);
    }

    void requestToggleKeepBelow() override
    {
        perform_window_operation(m_client, base::options_qobject::KeepBelowOp);
    }

    void requestToggleOnAllDesktops() override
    {
        perform_window_operation(m_client, base::options_qobject::OnAllDesktopsOp);
    }

    // Deprecated.
    void requestToggleShade() override
    {
    }

    void showApplicationMenu(int actionId) override
    {
        decoration()->showApplicationMenu(actionId);
    }

    void update_size()
    {
        if (win::frame_to_client_size(m_client, m_client->geo.size()) == m_clientSize) {
            return;
        }

        auto deco_client = decoratedClient();

        auto const old_size = m_clientSize;
        m_clientSize = frame_to_client_size(m_client, m_client->geo.size());

        if (old_size.width() != m_clientSize.width()) {
            Q_EMIT deco_client->widthChanged(m_clientSize.width());
        }
        if (old_size.height() != m_clientSize.height()) {
            Q_EMIT deco_client->heightChanged(m_clientSize.height());
        }
        Q_EMIT deco_client->sizeChanged(m_clientSize);
    }

    Window* client()
    {
        return m_client;
    }

    renderer_t* renderer()
    {
        return m_renderer.get();
    }

    std::unique_ptr<render_data> move_renderer()
    {
        if (!m_renderer) {
            return {};
        }
        return m_renderer->move_data();
    }

    KDecoration2::DecoratedClient* decoratedClient()
    {
        return KDecoration2::DecoratedClientPrivate::client();
    }

    std::unique_ptr<client_impl_qobject> qobject;

private:
    void handle_compositing_toggled(bool active)
    {
        using space_t = std::decay_t<decltype(m_client->space)>;
        using render_t = typename space_t::base_t::render_t;

        m_renderer.reset();
        auto should_create_renderer = active;

        if constexpr (requires(render_t * render, render_window window) {
                          render->create_non_composited_deco(window);
                      }) {
            should_create_renderer = true;
        }

        if (should_create_renderer) {
            createRenderer();
        }

        decoration()->update();
    }

    void createRenderer()
    {
        assert(!m_renderer);
        m_renderer = std::make_unique<renderer_t>(this);
    }

    Window* m_client;
    QSize m_clientSize;
    std::unique_ptr<renderer_t> m_renderer;
    QMetaObject::Connection m_compositorToggledConnection;

    QString m_toolTipText;
    QTimer m_toolTipWakeUp;
    QDeadlineTimer m_toolTipFallAsleep;
    bool m_toolTipShowing = false;
};

}
