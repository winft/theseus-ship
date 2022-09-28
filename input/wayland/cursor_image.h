/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor_theme.h"

#include "base/wayland/server.h"
#include "kwin_export.h"
#include "main.h"
#include "win/space_qobject.h"
#include "win/window_qobject.h"

#include <KScreenLocker/KsldApp>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <memory>
#include <wayland-cursor.h>

namespace KWin::input::wayland
{

class KWIN_EXPORT cursor_image_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void changed();
};

template<typename Cursor, typename Redirect>
class cursor_image
{
public:
    using window_t = typename Redirect::platform_t::base_t::space_t::window_t;

    cursor_image(Redirect& redirect)
        : qobject{std::make_unique<cursor_image_qobject>()}
        , redirect{redirect}
    {
        QObject::connect(waylandServer()->seat(),
                         &Wrapland::Server::Seat::focusedPointerChanged,
                         qobject.get(),
                         [this] { update(); });
        QObject::connect(waylandServer()->seat(),
                         &Wrapland::Server::Seat::dragStarted,
                         qobject.get(),
                         [this] { updateDrag(); });
        QObject::connect(
            waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, qobject.get(), [this] {
                QObject::disconnect(m_drag.connection);
                reevaluteSource();
            });

        if (waylandServer()->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             qobject.get(),
                             [this] { reevaluteSource(); });
        }

        m_surfaceRenderedTimer.start();

        // Loading the theme is delayed to end of startup because we depend on the client
        // connection.
        // TODO(romangg): Instead load the theme without client connection and setup directly.
        QObject::connect(
            kwinApp(), &Application::startup_finished, qobject.get(), [this] { setup_theme(); });
    }

    void setEffectsOverrideCursor(Qt::CursorShape shape)
    {
        loadThemeCursor(shape, &m_effectsCursor);
        if (m_currentSource == CursorSource::EffectsOverride) {
            Q_EMIT qobject->changed();
        }
        reevaluteSource();
    }

    void removeEffectsOverrideCursor()
    {
        reevaluteSource();
    }

    void setWindowSelectionCursor(const QByteArray& shape)
    {
        if (shape.isEmpty()) {
            loadThemeCursor(Qt::CrossCursor, &m_windowSelectionCursor);
        } else {
            loadThemeCursor(shape, &m_windowSelectionCursor);
        }
        if (m_currentSource == CursorSource::WindowSelector) {
            Q_EMIT qobject->changed();
        }
        reevaluteSource();
    }

    void removeWindowSelectionCursor()
    {
        reevaluteSource();
    }

    QImage image() const
    {
        switch (m_currentSource) {
        case CursorSource::EffectsOverride:
            return m_effectsCursor.image;
        case CursorSource::MoveResize:
            return m_moveResizeCursor.image;
        case CursorSource::LockScreen:
        case CursorSource::PointerSurface:
            // lockscreen also uses server cursor image
            return m_serverCursor.image;
        case CursorSource::Decoration:
            return m_decorationCursor.image;
        case CursorSource::DragAndDrop:
            return m_drag.cursor.image;
        case CursorSource::Fallback:
            return m_fallbackCursor.image;
        case CursorSource::WindowSelector:
            return m_windowSelectionCursor.image;
        default:
            Q_UNREACHABLE();
        }
    }

    QPoint hotSpot() const
    {
        switch (m_currentSource) {
        case CursorSource::EffectsOverride:
            return m_effectsCursor.hotSpot;
        case CursorSource::MoveResize:
            return m_moveResizeCursor.hotSpot;
        case CursorSource::LockScreen:
        case CursorSource::PointerSurface:
            // lockscreen also uses server cursor image
            return m_serverCursor.hotSpot;
        case CursorSource::Decoration:
            return m_decorationCursor.hotSpot;
        case CursorSource::DragAndDrop:
            return m_drag.cursor.hotSpot;
        case CursorSource::Fallback:
            return m_fallbackCursor.hotSpot;
        case CursorSource::WindowSelector:
            return m_windowSelectionCursor.hotSpot;
        default:
            Q_UNREACHABLE();
        }
    }

    void markAsRendered()
    {
        auto seat = waylandServer()->seat();

        if (m_currentSource == CursorSource::DragAndDrop) {
            auto p = seat->drags().get_source().pointer;
            if (!p) {
                return;
            }
            auto c = p->cursor();
            if (!c) {
                return;
            }
            auto cursorSurface = c->surface();
            if (cursorSurface.isNull()) {
                return;
            }
            cursorSurface->frameRendered(m_surfaceRenderedTimer.elapsed());
            return;
        }
        if (m_currentSource != CursorSource::LockScreen
            && m_currentSource != CursorSource::PointerSurface) {
            return;
        }

        if (!seat->hasPointer()) {
            return;
        }

        auto const pointer_focus = seat->pointers().get_focus();
        if (pointer_focus.devices.empty()) {
            return;
        }

        auto c = pointer_focus.devices.front()->cursor();
        if (!c) {
            return;
        }
        auto cursorSurface = c->surface();
        if (cursorSurface.isNull()) {
            return;
        }
        cursorSurface->frameRendered(m_surfaceRenderedTimer.elapsed());
    }

    void unset_deco()
    {
        QObject::disconnect(m_decorationConnection);
        m_decorationConnection = QMetaObject::Connection();
        updateDecorationCursor();
    }

    template<typename Deco>
    void set_deco(Deco& deco)
    {
        QObject::disconnect(m_decorationConnection);
        auto win = deco.client();
        assert(win);
        m_decorationConnection = QObject::connect(win->qobject.get(),
                                                  &win::window_qobject::moveResizeCursorChanged,
                                                  qobject.get(),
                                                  [this] { updateDecorationCursor(); });
        updateDecorationCursor();
    }

    std::unique_ptr<cursor_image_qobject> qobject;

private:
    void setup_theme()
    {
        QObject::connect(redirect.space.qobject.get(),
                         &win::space_qobject::wayland_window_added,
                         qobject.get(),
                         [this](auto win_id) {
                             auto win = redirect.space.windows_map.at(win_id);
                             setup_move_resize(win);
                         });

        // TODO(romangg): can we load the fallback cursor earlier in the ctor already?
        loadThemeCursor(Qt::ArrowCursor, &m_fallbackCursor);
        if (m_cursorTheme) {
            QObject::connect(m_cursorTheme->qobject.get(),
                             &cursor_theme_qobject::themeChanged,
                             qobject.get(),
                             [this] {
                                 m_cursors.clear();
                                 m_cursorsByName.clear();
                                 loadThemeCursor(Qt::ArrowCursor, &m_fallbackCursor);
                                 updateDecorationCursor();
                                 updateMoveResize();
                                 // TODO: update effects
                             });
        }

        auto const clients = redirect.space.windows;
        std::for_each(clients.begin(), clients.end(), [this](auto win) { setup_move_resize(win); });

        QObject::connect(redirect.space.qobject.get(),
                         &win::space_qobject::clientAdded,
                         qobject.get(),
                         [this](auto win_id) {
                             auto win = redirect.space.windows_map.at(win_id);
                             setup_move_resize(win);
                         });

        Q_EMIT qobject->changed();
    }

    void setup_move_resize(window_t* window)
    {
        if (!window->control) {
            return;
        }
        QObject::connect(window->qobject.get(),
                         &win::window_qobject::moveResizedChanged,
                         qobject.get(),
                         [this] { updateMoveResize(); });
        QObject::connect(window->qobject.get(),
                         &win::window_qobject::moveResizeCursorChanged,
                         qobject.get(),
                         [this] { updateMoveResize(); });
    }

    void reevaluteSource()
    {
        if (waylandServer()->seat()->drags().is_pointer_drag()) {
            // TODO: touch drag?
            setSource(CursorSource::DragAndDrop);
            return;
        }
        if (kwinApp()->is_screen_locked()) {
            setSource(CursorSource::LockScreen);
            return;
        }
        if (redirect.isSelectingWindow()) {
            setSource(CursorSource::WindowSelector);
            return;
        }
        if (auto& effects = redirect.platform.base.render->compositor->effects;
            effects && effects->isMouseInterception()) {
            setSource(CursorSource::EffectsOverride);
            return;
        }
        if (redirect.space.move_resize_window) {
            setSource(CursorSource::MoveResize);
            return;
        }
        if (redirect.pointer->focus.deco) {
            setSource(CursorSource::Decoration);
            return;
        }
        if (redirect.pointer->focus.window
            && !waylandServer()->seat()->pointers().get_focus().devices.empty()) {
            setSource(CursorSource::PointerSurface);
            return;
        }
        setSource(CursorSource::Fallback);
    }

    void update()
    {
        if (redirect.pointer->cursor_update_blocking) {
            return;
        }
        using namespace Wrapland::Server;
        QObject::disconnect(m_serverCursor.connection);

        auto const pointer_focus = waylandServer()->seat()->pointers().get_focus();
        if (pointer_focus.devices.empty()) {
            m_serverCursor.connection = QMetaObject::Connection();
            reevaluteSource();
            return;
        }

        m_serverCursor.connection = QObject::connect(pointer_focus.devices.front(),
                                                     &Pointer::cursorChanged,
                                                     qobject.get(),
                                                     [this] { updateServerCursor(); });
    }

    void updateServerCursor()
    {
        m_serverCursor.image = QImage();
        m_serverCursor.hotSpot = QPoint();
        reevaluteSource();
        const bool needsEmit = m_currentSource == CursorSource::LockScreen
            || m_currentSource == CursorSource::PointerSurface;

        auto seat = waylandServer()->seat();
        if (!seat->hasPointer()) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }

        auto const pointer_focus = seat->pointers().get_focus();
        if (pointer_focus.devices.empty()) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }

        auto c = pointer_focus.devices.front()->cursor();
        if (!c) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto cursorSurface = c->surface();
        if (cursorSurface.isNull()) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto buffer = cursorSurface.data()->state().buffer;
        if (!buffer) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        m_serverCursor.hotSpot = c->hotspot();
        m_serverCursor.image = buffer->shmImage()->createQImage().copy();
        m_serverCursor.image.setDevicePixelRatio(cursorSurface->state().scale);
        if (needsEmit) {
            Q_EMIT qobject->changed();
        }
    }

    void updateDecorationCursor()
    {
        m_decorationCursor.image = QImage();
        m_decorationCursor.hotSpot = QPoint();

        auto deco = redirect.pointer->focus.deco;
        if (auto c = deco ? deco->client() : nullptr) {
            loadThemeCursor(c->control->move_resize.cursor, &m_decorationCursor);
            if (m_currentSource == CursorSource::Decoration) {
                Q_EMIT qobject->changed();
            }
        }
        reevaluteSource();
    }

    void updateMoveResize()
    {
        m_moveResizeCursor.image = QImage();
        m_moveResizeCursor.hotSpot = QPoint();
        if (auto window = redirect.space.move_resize_window) {
            loadThemeCursor(window->control->move_resize.cursor, &m_moveResizeCursor);
            if (m_currentSource == CursorSource::MoveResize) {
                Q_EMIT qobject->changed();
            }
        }
        reevaluteSource();
    }

    void updateDrag()
    {
        using namespace Wrapland::Server;
        QObject::disconnect(m_drag.connection);
        m_drag.cursor.image = QImage();
        m_drag.cursor.hotSpot = QPoint();
        reevaluteSource();
        if (auto p = waylandServer()->seat()->drags().get_source().pointer) {
            m_drag.connection = QObject::connect(
                p, &Pointer::cursorChanged, qobject.get(), [this] { updateDragCursor(); });
        } else {
            m_drag.connection = QMetaObject::Connection();
        }
        updateDragCursor();
    }

    void updateDragCursor()
    {
        m_drag.cursor.image = QImage();
        m_drag.cursor.hotSpot = QPoint();
        const bool needsEmit = m_currentSource == CursorSource::DragAndDrop;
        QImage additionalIcon;
        if (auto drag_icon = waylandServer()->seat()->drags().get_source().surfaces.icon) {
            if (auto buffer = drag_icon->state().buffer) {
                // TODO: Check std::optional?
                additionalIcon = buffer->shmImage()->createQImage().copy();
                additionalIcon.setOffset(drag_icon->state().offset);
            }
        }
        auto p = waylandServer()->seat()->drags().get_source().pointer;
        if (!p) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto c = p->cursor();
        if (!c) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto cursorSurface = c->surface();
        if (cursorSurface.isNull()) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto buffer = cursorSurface.data()->state().buffer;
        if (!buffer) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        m_drag.cursor.hotSpot = c->hotspot();

        if (additionalIcon.isNull()) {
            m_drag.cursor.image = buffer->shmImage()->createQImage().copy();
            m_drag.cursor.image.setDevicePixelRatio(cursorSurface->state().scale);
        } else {
            QRect cursorRect = buffer->shmImage()->createQImage().rect();
            QRect iconRect = additionalIcon.rect();

            if (-m_drag.cursor.hotSpot.x() < additionalIcon.offset().x()) {
                iconRect.moveLeft(m_drag.cursor.hotSpot.x() - additionalIcon.offset().x());
            } else {
                cursorRect.moveLeft(-additionalIcon.offset().x() - m_drag.cursor.hotSpot.x());
            }
            if (-m_drag.cursor.hotSpot.y() < additionalIcon.offset().y()) {
                iconRect.moveTop(m_drag.cursor.hotSpot.y() - additionalIcon.offset().y());
            } else {
                cursorRect.moveTop(-additionalIcon.offset().y() - m_drag.cursor.hotSpot.y());
            }

            m_drag.cursor.image
                = QImage(cursorRect.united(iconRect).size(), QImage::Format_ARGB32_Premultiplied);
            m_drag.cursor.image.setDevicePixelRatio(cursorSurface->state().scale);
            m_drag.cursor.image.fill(Qt::transparent);
            QPainter p(&m_drag.cursor.image);
            p.drawImage(iconRect, additionalIcon);
            p.drawImage(cursorRect, buffer->shmImage()->createQImage());
            p.end();
        }

        if (needsEmit) {
            Q_EMIT qobject->changed();
        }
        // TODO: add the cursor image
    }

    void loadTheme()
    {
        if (m_cursorTheme) {
            return;
        }

        // check whether we can create it
        if (waylandServer()->internal_connection.shm) {
            m_cursorTheme = std::make_unique<cursor_theme<Cursor>>(
                static_cast<Cursor&>(*redirect.cursor), waylandServer()->internal_connection.shm);
            QObject::connect(waylandServer(),
                             &base::wayland::server::terminating_internal_client_connection,
                             qobject.get(),
                             [this] { m_cursorTheme.reset(); });
        }
    }

    struct Image {
        QImage image;
        QPoint hotSpot;
    };

    void loadThemeCursor(cursor_shape shape, Image* image)
    {
        loadThemeCursor(shape, m_cursors, image);
    }

    void loadThemeCursor(const QByteArray& shape, Image* image)
    {
        loadThemeCursor(shape, m_cursorsByName, image);
    }

    template<typename T>
    void loadThemeCursor(const T& shape, QHash<T, Image>& cursors, Image* image)
    {
        loadTheme();

        if (!m_cursorTheme) {
            return;
        }

        auto it = cursors.constFind(shape);
        if (it == cursors.constEnd()) {
            image->image = QImage();
            image->hotSpot = QPoint();
            wl_cursor_image* cursor = m_cursorTheme->get(shape);
            if (!cursor) {
                return;
            }
            wl_buffer* b = wl_cursor_image_get_buffer(cursor);
            if (!b) {
                return;
            }
            waylandServer()->internal_connection.client->flush();
            waylandServer()->dispatch();
            auto buffer = Wrapland::Server::Buffer::get(
                waylandServer()->display.get(),
                waylandServer()->internal_connection.server->getResource(
                    Wrapland::Client::Buffer::getId(b)));
            if (!buffer) {
                return;
            }
            auto scale = redirect.platform.base.topology.max_scale;
            int hotSpotX = qRound(cursor->hotspot_x / scale);
            int hotSpotY = qRound(cursor->hotspot_y / scale);
            QImage img = buffer->shmImage()->createQImage().copy();
            img.setDevicePixelRatio(scale);
            it = decltype(it)(cursors.insert(shape, {img, QPoint(hotSpotX, hotSpotY)}));
        }

        image->hotSpot = it.value().hotSpot;
        image->image = it.value().image;
    };

    enum class CursorSource {
        LockScreen,
        EffectsOverride,
        MoveResize,
        PointerSurface,
        Decoration,
        DragAndDrop,
        Fallback,
        WindowSelector
    };

    void setSource(CursorSource source)
    {
        if (m_currentSource == source) {
            return;
        }
        m_currentSource = source;
        Q_EMIT qobject->changed();
    }

    CursorSource m_currentSource = CursorSource::Fallback;
    std::unique_ptr<cursor_theme<Cursor>> m_cursorTheme;

    struct {
        QMetaObject::Connection connection;
        QImage image;
        QPoint hotSpot;
    } m_serverCursor;

    Image m_effectsCursor;
    Image m_decorationCursor;
    QMetaObject::Connection m_decorationConnection;
    Image m_fallbackCursor;
    Image m_moveResizeCursor;
    Image m_windowSelectionCursor;
    QHash<cursor_shape, Image> m_cursors;
    QHash<QByteArray, Image> m_cursorsByName;
    QElapsedTimer m_surfaceRenderedTimer;
    struct {
        Image cursor;
        QMetaObject::Connection connection;
    } m_drag;

    Redirect& redirect;
};

}
