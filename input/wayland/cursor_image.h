/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <memory>

namespace KWin
{
class Toplevel;

namespace input
{
class cursor_shape;
class pointer_redirect;

namespace wayland
{

class cursor_theme;

class KWIN_EXPORT cursor_image : public QObject
{
    Q_OBJECT
public:
    cursor_image();
    ~cursor_image() override;

    void setEffectsOverrideCursor(Qt::CursorShape shape);
    void removeEffectsOverrideCursor();
    void setWindowSelectionCursor(const QByteArray& shape);
    void removeWindowSelectionCursor();

    QImage image() const;
    QPoint hotSpot() const;
    void markAsRendered();

    void updateDecoration();

Q_SIGNALS:
    void changed();

private:
    void setup_theme();
    void setup_move_resize(Toplevel* window);

    void reevaluteSource();
    void update();
    void updateServerCursor();
    void updateDecorationCursor();
    void updateMoveResize();
    void updateDrag();
    void updateDragCursor();
    void loadTheme();
    struct Image {
        QImage image;
        QPoint hotSpot;
    };
    void loadThemeCursor(cursor_shape shape, Image* image);
    void loadThemeCursor(const QByteArray& shape, Image* image);
    template<typename T>
    void loadThemeCursor(const T& shape, QHash<T, Image>& cursors, Image* image);

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
    void setSource(CursorSource source);

    CursorSource m_currentSource = CursorSource::Fallback;
    std::unique_ptr<cursor_theme> m_cursorTheme;

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
};

}
}
}
