/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor_image.h"

#include "cursor_theme.h"
#include <input/pointer_redirect.h>

#include <effects.h>
#include <screens.h>
#include <wayland_server.h>
#include <win/control.h>
#include <win/wayland/window.h>
#include <win/x11/window.h>

#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <KScreenLocker/KsldApp>
#include <QPainter>
#include <wayland-cursor.h>

namespace KWin::input::wayland
{

cursor_image::cursor_image()
    : QObject()
{
    connect(waylandServer()->seat(),
            &Wrapland::Server::Seat::focusedPointerChanged,
            this,
            &cursor_image::update);
    connect(waylandServer()->seat(),
            &Wrapland::Server::Seat::dragStarted,
            this,
            &cursor_image::updateDrag);
    connect(waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, this, [this] {
        disconnect(m_drag.connection);
        reevaluteSource();
    });

    if (waylandServer()->hasScreenLockerIntegration()) {
        connect(ScreenLocker::KSldApp::self(),
                &ScreenLocker::KSldApp::lockStateChanged,
                this,
                &cursor_image::reevaluteSource);
    }

    m_surfaceRenderedTimer.start();

    connect(waylandServer(), &WaylandServer::window_added, this, &cursor_image::setup_move_resize);

    assert(!workspace());
    connect(kwinApp(), &Application::workspaceCreated, this, &cursor_image::setup_workspace);
}

cursor_image::~cursor_image() = default;

void cursor_image::setup_workspace()
{
    // TODO(romangg): can we load the fallback cursor earlier in the ctor already?
    loadThemeCursor(Qt::ArrowCursor, &m_fallbackCursor);
    if (m_cursorTheme) {
        connect(m_cursorTheme, &cursor_theme::themeChanged, this, [this] {
            m_cursors.clear();
            m_cursorsByName.clear();
            loadThemeCursor(Qt::ArrowCursor, &m_fallbackCursor);
            updateDecorationCursor();
            updateMoveResize();
            // TODO: update effects
        });
    }

    auto const clients = workspace()->allClientList();
    std::for_each(clients.begin(), clients.end(), [this](auto win) { setup_move_resize(win); });

    connect(workspace(), &Workspace::clientAdded, this, &cursor_image::setup_move_resize);

    Q_EMIT changed();
}

void cursor_image::setup_move_resize(Toplevel* window)
{
    if (!window->control) {
        return;
    }
    connect(window, &Toplevel::moveResizedChanged, this, &cursor_image::updateMoveResize);
    connect(window, &Toplevel::moveResizeCursorChanged, this, &cursor_image::updateMoveResize);
}

void cursor_image::markAsRendered()
{
    if (m_currentSource == CursorSource::DragAndDrop) {
        // always sending a frame rendered to the drag icon surface to not freeze QtWayland (see
        // https://bugreports.qt.io/browse/QTBUG-51599 )
        if (auto ddi = waylandServer()->seat()->drags().get_source().dev) {
            if (auto s = ddi->icon()) {
                s->frameRendered(m_surfaceRenderedTimer.elapsed());
            }
        }
        auto p = waylandServer()->seat()->drags().get_source().pointer;
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

    auto const pointer_focus = waylandServer()->seat()->pointers().get_focus();
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

void cursor_image::update()
{
    if (kwinApp()->input->redirect->pointer()->s_cursorUpdateBlocking) {
        return;
    }
    using namespace Wrapland::Server;
    disconnect(m_serverCursor.connection);

    auto const pointer_focus = waylandServer()->seat()->pointers().get_focus();
    if (pointer_focus.devices.empty()) {
        m_serverCursor.connection = QMetaObject::Connection();
        reevaluteSource();
        return;
    }

    m_serverCursor.connection = connect(pointer_focus.devices.front(),
                                        &Pointer::cursorChanged,
                                        this,
                                        &cursor_image::updateServerCursor);
}

void cursor_image::updateDecoration()
{
    disconnect(m_decorationConnection);
    auto deco = kwinApp()->input->redirect->pointer()->decoration();
    auto c = deco ? deco->client() : nullptr;
    if (c) {
        m_decorationConnection = connect(
            c, &Toplevel::moveResizeCursorChanged, this, &cursor_image::updateDecorationCursor);
    } else {
        m_decorationConnection = QMetaObject::Connection();
    }
    updateDecorationCursor();
}

void cursor_image::updateDecorationCursor()
{
    m_decorationCursor.image = QImage();
    m_decorationCursor.hotSpot = QPoint();

    auto deco = kwinApp()->input->redirect->pointer()->decoration();
    if (auto c = deco ? deco->client() : nullptr) {
        loadThemeCursor(c->control->move_resize().cursor, &m_decorationCursor);
        if (m_currentSource == CursorSource::Decoration) {
            emit changed();
        }
    }
    reevaluteSource();
}

void cursor_image::updateMoveResize()
{
    m_moveResizeCursor.image = QImage();
    m_moveResizeCursor.hotSpot = QPoint();
    if (auto window = workspace()->moveResizeClient()) {
        loadThemeCursor(window->control->move_resize().cursor, &m_moveResizeCursor);
        if (m_currentSource == CursorSource::MoveResize) {
            emit changed();
        }
    }
    reevaluteSource();
}

void cursor_image::updateServerCursor()
{
    m_serverCursor.image = QImage();
    m_serverCursor.hotSpot = QPoint();
    reevaluteSource();
    const bool needsEmit = m_currentSource == CursorSource::LockScreen
        || m_currentSource == CursorSource::PointerSurface;

    auto const pointer_focus = waylandServer()->seat()->pointers().get_focus();
    if (pointer_focus.devices.empty()) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }

    auto c = pointer_focus.devices.front()->cursor();
    if (!c) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    auto cursorSurface = c->surface();
    if (cursorSurface.isNull()) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    auto buffer = cursorSurface.data()->buffer();
    if (!buffer) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    m_serverCursor.hotSpot = c->hotspot();
    m_serverCursor.image = buffer->shmImage()->createQImage().copy();
    m_serverCursor.image.setDevicePixelRatio(cursorSurface->scale());
    if (needsEmit) {
        emit changed();
    }
}

void cursor_image::loadTheme()
{
    if (m_cursorTheme) {
        return;
    }
    // check whether we can create it
    if (waylandServer()->internalShmPool()) {
        m_cursorTheme = new cursor_theme(waylandServer()->internalShmPool(), this);
        connect(waylandServer(), &WaylandServer::terminatingInternalClientConnection, this, [this] {
            delete m_cursorTheme;
            m_cursorTheme = nullptr;
        });
    }
}

void cursor_image::setEffectsOverrideCursor(Qt::CursorShape shape)
{
    loadThemeCursor(shape, &m_effectsCursor);
    if (m_currentSource == CursorSource::EffectsOverride) {
        emit changed();
    }
    reevaluteSource();
}

void cursor_image::removeEffectsOverrideCursor()
{
    reevaluteSource();
}

void cursor_image::setWindowSelectionCursor(const QByteArray& shape)
{
    if (shape.isEmpty()) {
        loadThemeCursor(Qt::CrossCursor, &m_windowSelectionCursor);
    } else {
        loadThemeCursor(shape, &m_windowSelectionCursor);
    }
    if (m_currentSource == CursorSource::WindowSelector) {
        emit changed();
    }
    reevaluteSource();
}

void cursor_image::removeWindowSelectionCursor()
{
    reevaluteSource();
}

void cursor_image::updateDrag()
{
    using namespace Wrapland::Server;
    disconnect(m_drag.connection);
    m_drag.cursor.image = QImage();
    m_drag.cursor.hotSpot = QPoint();
    reevaluteSource();
    if (auto p = waylandServer()->seat()->drags().get_source().pointer) {
        m_drag.connection
            = connect(p, &Pointer::cursorChanged, this, &cursor_image::updateDragCursor);
    } else {
        m_drag.connection = QMetaObject::Connection();
    }
    updateDragCursor();
}

void cursor_image::updateDragCursor()
{
    m_drag.cursor.image = QImage();
    m_drag.cursor.hotSpot = QPoint();
    const bool needsEmit = m_currentSource == CursorSource::DragAndDrop;
    QImage additionalIcon;
    if (auto ddi = waylandServer()->seat()->drags().get_source().dev) {
        if (auto dragIcon = ddi->icon()) {
            if (auto buffer = dragIcon->buffer()) {
                // TODO: Check std::optional?
                additionalIcon = buffer->shmImage()->createQImage().copy();
                additionalIcon.setOffset(dragIcon->offset());
            }
        }
    }
    auto p = waylandServer()->seat()->drags().get_source().pointer;
    if (!p) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    auto c = p->cursor();
    if (!c) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    auto cursorSurface = c->surface();
    if (cursorSurface.isNull()) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    auto buffer = cursorSurface.data()->buffer();
    if (!buffer) {
        if (needsEmit) {
            emit changed();
        }
        return;
    }
    m_drag.cursor.hotSpot = c->hotspot();

    if (additionalIcon.isNull()) {
        m_drag.cursor.image = buffer->shmImage()->createQImage().copy();
        m_drag.cursor.image.setDevicePixelRatio(cursorSurface->scale());
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
        m_drag.cursor.image.setDevicePixelRatio(cursorSurface->scale());
        m_drag.cursor.image.fill(Qt::transparent);
        QPainter p(&m_drag.cursor.image);
        p.drawImage(iconRect, additionalIcon);
        p.drawImage(cursorRect, buffer->shmImage()->createQImage());
        p.end();
    }

    if (needsEmit) {
        emit changed();
    }
    // TODO: add the cursor image
}

void cursor_image::loadThemeCursor(cursor_shape shape, Image* image)
{
    loadThemeCursor(shape, m_cursors, image);
}

void cursor_image::loadThemeCursor(const QByteArray& shape, Image* image)
{
    loadThemeCursor(shape, m_cursorsByName, image);
}

template<typename T>
void cursor_image::loadThemeCursor(const T& shape, QHash<T, Image>& cursors, Image* image)
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
        waylandServer()->internalClientConection()->flush();
        waylandServer()->dispatch();
        auto buffer = Wrapland::Server::Buffer::get(
            waylandServer()->display(),
            waylandServer()->internalConnection()->getResource(Wrapland::Client::Buffer::getId(b)));
        if (!buffer) {
            return;
        }
        auto scale = screens()->maxScale();
        int hotSpotX = qRound(cursor->hotspot_x / scale);
        int hotSpotY = qRound(cursor->hotspot_y / scale);
        QImage img = buffer->shmImage()->createQImage().copy();
        img.setDevicePixelRatio(scale);
        it = decltype(it)(cursors.insert(shape, {img, QPoint(hotSpotX, hotSpotY)}));
    }
    image->hotSpot = it.value().hotSpot;
    image->image = it.value().image;
}

void cursor_image::reevaluteSource()
{
    if (waylandServer()->seat()->drags().is_pointer_drag()) {
        // TODO: touch drag?
        setSource(CursorSource::DragAndDrop);
        return;
    }
    if (waylandServer()->isScreenLocked()) {
        setSource(CursorSource::LockScreen);
        return;
    }
    if (kwinApp()->input->redirect->isSelectingWindow()) {
        setSource(CursorSource::WindowSelector);
        return;
    }
    if (effects && static_cast<EffectsHandlerImpl*>(effects)->isMouseInterception()) {
        setSource(CursorSource::EffectsOverride);
        return;
    }
    if (workspace() && workspace()->moveResizeClient()) {
        setSource(CursorSource::MoveResize);
        return;
    }
    if (kwinApp()->input->redirect->pointer()->decoration()) {
        setSource(CursorSource::Decoration);
        return;
    }
    if (kwinApp()->input->redirect->pointer()->focus()
        && !waylandServer()->seat()->pointers().get_focus().devices.empty()) {
        setSource(CursorSource::PointerSurface);
        return;
    }
    setSource(CursorSource::Fallback);
}

void cursor_image::setSource(CursorSource source)
{
    if (m_currentSource == source) {
        return;
    }
    m_currentSource = source;
    emit changed();
}

QImage cursor_image::image() const
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

QPoint cursor_image::hotSpot() const
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

}
