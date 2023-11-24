/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor_theme.h"

#include "base/wayland/screen_lock.h"
#include "base/wayland/server.h"
#include "kwin_export.h"
#include "win/space_qobject.h"
#include "win/window_qobject.h"

#include <KScreenLocker/KsldApp>
#include <QElapsedTimer>
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
#include <map>
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

template<typename Cursor>
class cursor_image
{
public:
    using window_t = typename Cursor::redirect_t::platform_t::base_t::space_t::window_t;

    cursor_image(Cursor& cursor)
        : qobject{std::make_unique<cursor_image_qobject>()}
        , cursor{cursor}
    {
        auto& redirect = cursor.redirect;

        QObject::connect(redirect.platform.base.server->seat(),
                         &Wrapland::Server::Seat::focusedPointerChanged,
                         qobject.get(),
                         [this] { update(); });
        QObject::connect(redirect.platform.base.server->seat(),
                         &Wrapland::Server::Seat::dragStarted,
                         qobject.get(),
                         [this] { updateDrag(); });
        QObject::connect(redirect.platform.base.server->seat(),
                         &Wrapland::Server::Seat::dragEnded,
                         qobject.get(),
                         [this] {
                             QObject::disconnect(m_drag.connection);
                             reevaluteSource();
                         });

        if (redirect.platform.base.server->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             qobject.get(),
                             [this] { reevaluteSource(); });
        }

        m_surfaceRenderedTimer.start();

        // Loading the theme is delayed because we depend on the client connection.
        // TODO(romangg): Instead load the theme without client connection and setup directly.
        QObject::connect(redirect.platform.base.server->qobject.get(),
                         &base::wayland::server_qobject::internal_client_available,
                         qobject.get(),
                         [this] { setup_theme(); });
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

    void setWindowSelectionCursor(std::string const& shape)
    {
        if (shape.empty()) {
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
        auto seat = cursor.redirect.platform.base.server->seat();

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
            if (!cursorSurface) {
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
        if (!cursorSurface) {
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
        QObject::connect(cursor.redirect.space.qobject.get(),
                         &win::space_qobject::wayland_window_added,
                         qobject.get(),
                         [this](auto win_id) {
                             std::visit(overload{[this](auto&& win) { setup_move_resize(win); }},
                                        cursor.redirect.space.windows_map.at(win_id));
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

        auto const clients = cursor.redirect.space.windows;
        std::for_each(clients.begin(), clients.end(), [this](auto win) {
            std::visit(overload{[this](auto&& win) { setup_move_resize(win); }}, win);
        });

        QObject::connect(cursor.redirect.space.qobject.get(),
                         &win::space_qobject::clientAdded,
                         qobject.get(),
                         [this](auto win_id) {
                             std::visit(overload{[this](auto&& win) { setup_move_resize(win); }},
                                        cursor.redirect.space.windows_map.at(win_id));
                         });

        Q_EMIT qobject->changed();
    }

    template<typename Win>
    void setup_move_resize(Win* window)
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
        if (cursor.redirect.platform.base.server->seat()->drags().is_pointer_drag()) {
            // TODO: touch drag?
            setSource(CursorSource::DragAndDrop);
            return;
        }
        if (base::wayland::is_screen_locked(cursor.redirect.platform.base)) {
            setSource(CursorSource::LockScreen);
            return;
        }
        if (cursor.redirect.isSelectingWindow()) {
            setSource(CursorSource::WindowSelector);
            return;
        }
        if (auto& effects = cursor.redirect.platform.base.render->effects;
            effects && effects->isMouseInterception()) {
            setSource(CursorSource::EffectsOverride);
            return;
        }
        if (cursor.redirect.space.move_resize_window) {
            setSource(CursorSource::MoveResize);
            return;
        }
        if (cursor.redirect.pointer->focus.deco.client) {
            setSource(CursorSource::Decoration);
            return;
        }
        if (cursor.redirect.pointer->focus.window
            && !cursor.redirect.platform.base.server->seat()
                    ->pointers()
                    .get_focus()
                    .devices.empty()) {
            setSource(CursorSource::PointerSurface);
            return;
        }
        setSource(CursorSource::Fallback);
    }

    void update()
    {
        if (cursor.redirect.pointer->cursor_update_blocking) {
            return;
        }
        using namespace Wrapland::Server;
        QObject::disconnect(m_serverCursor.connection);

        auto const pointer_focus
            = cursor.redirect.platform.base.server->seat()->pointers().get_focus();
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

        auto seat = cursor.redirect.platform.base.server->seat();
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
        if (!cursorSurface) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto buffer = cursorSurface->state().buffer;
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

        if (auto win = cursor.redirect.pointer->at.window) {
            std::visit(overload{[&](auto&& win) {
                           if (!cursor.redirect.pointer->focus.deco.client) {
                               return;
                           }
                           loadThemeCursor(win->control->move_resize.cursor, &m_decorationCursor);
                           if (m_currentSource == CursorSource::Decoration) {
                               Q_EMIT qobject->changed();
                           }
                       }},
                       *win);
        }
        reevaluteSource();
    }

    void updateMoveResize()
    {
        m_moveResizeCursor.image = QImage();
        m_moveResizeCursor.hotSpot = QPoint();

        if (auto& mov_res = cursor.redirect.space.move_resize_window) {
            auto cursor = std::visit(
                overload{[&](auto&& win) { return win->control->move_resize.cursor; }}, *mov_res);
            loadThemeCursor(cursor, &m_moveResizeCursor);
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
        if (auto p = cursor.redirect.platform.base.server->seat()->drags().get_source().pointer) {
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
        if (auto drag_icon
            = cursor.redirect.platform.base.server->seat()->drags().get_source().surfaces.icon) {
            if (auto buffer = drag_icon->state().buffer) {
                // TODO: Check std::optional?
                additionalIcon = buffer->shmImage()->createQImage().copy();
                additionalIcon.setOffset(drag_icon->state().offset);
            }
        }
        auto p = cursor.redirect.platform.base.server->seat()->drags().get_source().pointer;
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
        if (!cursorSurface) {
            if (needsEmit) {
                Q_EMIT qobject->changed();
            }
            return;
        }
        auto buffer = cursorSurface->state().buffer;
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
        if (cursor.redirect.platform.base.server->internal_connection.shm) {
            m_cursorTheme = std::make_unique<cursor_theme<Cursor>>(
                static_cast<Cursor&>(*cursor.redirect.cursor),
                cursor.redirect.platform.base.server->internal_connection.shm);
            QObject::connect(cursor.redirect.platform.base.server->qobject.get(),
                             &base::wayland::server_qobject::terminating_internal_client_connection,
                             qobject.get(),
                             [this] { m_cursorTheme.reset(); });
        }
    }

    struct Image {
        QImage image;
        QPoint hotSpot;
    };

    void loadThemeCursor(win::cursor_shape shape, Image* image)
    {
        loadThemeCursor(shape, m_cursors, image);
    }

    void loadThemeCursor(std::string const& shape, Image* image)
    {
        loadThemeCursor(shape, m_cursorsByName, image);
    }

    template<typename T>
    void loadThemeCursor(T const& shape, std::map<T, Image>& cursors, Image* image)
    {
        loadTheme();

        if (!m_cursorTheme) {
            return;
        }

        auto it = cursors.find(shape);
        if (it == cursors.end()) {
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
            this->cursor.redirect.platform.base.server->internal_connection.client->flush();
            this->cursor.redirect.platform.base.server->dispatch();
            auto buffer = Wrapland::Server::Buffer::get(
                this->cursor.redirect.platform.base.server->display.get(),
                this->cursor.redirect.platform.base.server->internal_connection.server->getResource(
                    Wrapland::Client::Buffer::getId(b)));
            if (!buffer) {
                return;
            }
            auto scale = this->cursor.redirect.platform.base.topology.max_scale;
            int hotSpotX = qRound(cursor->hotspot_x / scale);
            int hotSpotY = qRound(cursor->hotspot_y / scale);
            QImage img = buffer->shmImage()->createQImage().copy();
            img.setDevicePixelRatio(scale);
            it = cursors.insert({shape, {img, QPoint(hotSpotX, hotSpotY)}}).first;
        }

        image->hotSpot = it->second.hotSpot;
        image->image = it->second.image;
    }

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
    std::map<win::cursor_shape, Image> m_cursors;
    std::map<std::string, Image> m_cursorsByName;
    QElapsedTimer m_surfaceRenderedTimer;
    struct {
        Image cursor;
        QMetaObject::Connection connection;
    } m_drag;

    Cursor& cursor;
};

}
