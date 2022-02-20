/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor_shape.h"

#include "kwinglobals.h"

#include <QObject>
#include <QPoint>
#include <xcb/xcb.h>

namespace KWin::input
{

/**
 * @short Replacement for QCursor.
 *
 * This class provides a similar API to QCursor and should be preferred inside KWin. It allows to
 * get the position and warp the mouse cursor with static methods just like QCursor. It also
 * provides the possibility to get an X11 cursor for a Qt::CursorShape - a functionality lost in Qt
 * 5's QCursor implementation.
 *
 * In addition the class provides a mouse polling facility as required by e.g. Effects and
 * ScreenEdges and emits signals when the mouse position changes. In opposite to QCursor this class
 * is a QObject and cannot be constructed. Instead it provides a singleton getter, though the most
 * important methods are wrapped in a static method, just like QCursor.
 *
 * The actual implementation is split into two parts: a system independent interface and a windowing
 * system specific subclass. So far only an X11 backend is implemented which uses query pointer to
 * fetch the position and warp pointer to set the position. It uses a timer based mouse polling and
 * can provide X11 cursors through the XCursor library.
 */
class KWIN_EXPORT cursor : public QObject
{
    Q_OBJECT
public:
    cursor();

    void start_mouse_polling();
    void stop_mouse_polling();

    /**
     * @brief Enables tracking changes of cursor images.
     *
     * After enabling cursor change tracking the signal image_changed will be emitted
     * whenever a change to the cursor image is recognized.
     *
     * Use stop_image_tracking to no longer emit this signal. Note: the signal will be
     * emitted until each call of this method has been matched with a call to stop_image_tracking.
     *
     * This tracking is not about pointer position tracking.
     * @see stop_image_tracking
     * @see image_changed
     */
    void start_image_tracking();

    /**
     * @brief Disables tracking changes of cursor images.
     *
     * Only call after using start_image_tracking.
     *
     * @see start_image_tracking
     */
    void stop_image_tracking();

    /**
     * @brief The name of the currently used cursor theme.
     *
     * @return QString const&
     */
    QString const& theme_name() const;

    /**
     * @brief The size of the currently used cursor theme.
     *
     * @return int
     */
    int theme_size() const;

    /**
     * @return list of alternative names for the cursor with @p name
     */
    QVector<QByteArray> alternative_names(QByteArray const& name) const;

    /**
     * Returns the current cursor position. This method does an update of the mouse position if
     * needed. It's save to call it multiple times.
     *
     * Implementing subclasses should prefer to use current_pos which is not performing a check
     * for update.
     */
    QPoint pos();

    /**
     * Warps the mouse cursor to new @p pos.
     */
    void set_pos(QPoint const& pos);
    void set_pos(int x, int y);

    virtual PlatformCursorImage platform_image() const = 0;

    virtual QImage image() const;
    virtual QPoint hotspot() const;
    virtual void mark_as_rendered();

    bool is_hidden() const;
    void show();
    void hide();

    virtual xcb_cursor_t x11_cursor(cursor_shape shape);

    /**
     * Notice: if available always use the cursor_shape variant to avoid cache duplicates for
     * ambiguous cursor names in the non existing cursor name specification
     */
    virtual xcb_cursor_t x11_cursor(QByteArray const& name);

Q_SIGNALS:
    void pos_changed(QPoint pos);
    void mouse_changed(QPoint const& pos,
                       QPoint const& oldpos,
                       Qt::MouseButtons buttons,
                       Qt::MouseButtons oldbuttons,
                       Qt::KeyboardModifiers modifiers,
                       Qt::KeyboardModifiers oldmodifiers);

    /**
     * @brief Signal emitted when the cursor image changes.
     *
     * To enable these signals use start_image_tracking.
     *
     * @see start_image_tracking
     * @see stop_image_tracking
     */
    void image_changed();
    void theme_changed();

protected:
    /**
     * Called from @ref pos() to allow syncing the internal position with the underlying
     * system's cursor position.
     */
    virtual void do_get_pos();
    /**
     * Performs the actual warping of the cursor.
     */
    virtual void do_set_pos();

    /**
     * Called from start_mouse_polling when the mouse polling gets activated. Base implementation
     * does nothing, inheriting classes can overwrite to e.g. start a timer.
     */
    virtual void do_start_mouse_polling();
    /**
     * Called from stop_mouse_polling when the mouse polling gets deactivated. Base implementation
     * does nothing, inheriting classes can overwrite to e.g. stop a timer.
     */
    virtual void do_stop_mouse_polling();

    bool is_image_tracking() const;
    /**
     * Called from start_image_tracking when cursor image tracking gets activated. Inheriting class
     * needs to overwrite to enable platform specific code for the tracking.
     */
    virtual void do_start_image_tracking();
    /**
     * Called from stop_image_tracking when cursor image tracking gets deactivated. Inheriting class
     * needs to overwrite to disable platform specific code for the tracking.
     */
    virtual void do_stop_image_tracking();

    virtual void do_hide();
    virtual void do_show();

    /**
     * Provides the actual internal cursor position to inheriting classes. If an inheriting class
     * needs access to the cursor position this method should be used instead of the static @ref
     * pos, as the static method syncs with the underlying system's cursor.
     */
    QPoint const& current_pos() const;

    /**
     * Updates the internal position to @p pos without warping the pointer as set_pos does.
     */
    void update_pos(QPoint const& pos);
    void update_pos(int x, int y);

private Q_SLOTS:
    void kglobal_settings_notify_change(int type, int arg);

private:
    void load_theme_settings();
    void update_theme(QString const& name, int size);
    void load_theme_from_kconfig();

    QHash<QByteArray, xcb_cursor_t> m_cursors;
    QPoint m_pos;
    int m_mousePollingCounter;
    int m_cursorTrackingCounter;
    QString m_themeName;
    int m_themeSize;
    int hide_count{0};
};

KWIN_EXPORT cursor* get_cursor();

}
