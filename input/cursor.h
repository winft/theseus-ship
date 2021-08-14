/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor_shape.h"

#include <kwinglobals.h>

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
    static cursor* self();

    cursor();
    ~cursor() override;

    void startMousePolling();
    void stopMousePolling();

    /**
     * @brief Enables tracking changes of cursor images.
     *
     * After enabling cursor change tracking the signal cursorChanged will be emitted
     * whenever a change to the cursor image is recognized.
     *
     * Use stopCursorTracking to no longer emit this signal. Note: the signal will be
     * emitted until each call of this method has been matched with a call to stopCursorTracking.
     *
     * This tracking is not about pointer position tracking.
     * @see stopCursorTracking
     * @see cursorChanged
     */
    void startCursorTracking();

    /**
     * @brief Disables tracking changes of cursor images.
     *
     * Only call after using startCursorTracking.
     *
     * @see startCursorTracking
     */
    void stopCursorTracking();

    /**
     * @brief The name of the currently used cursor theme.
     *
     * @return QString const&
     */
    QString const& themeName() const;

    /**
     * @brief The size of the currently used cursor theme.
     *
     * @return int
     */
    int themeSize() const;

    /**
     * @return list of alternative names for the cursor with @p name
     */
    QVector<QByteArray> cursorAlternativeNames(QByteArray const& name) const;

    /**
     * Returns the current cursor position. This method does an update of the mouse position if
     * needed. It's save to call it multiple times.
     *
     * Implementing subclasses should prefer to use currentPos which is not performing a check
     * for update.
     */
    static QPoint pos();

    /**
     * Warps the mouse cursor to new @p pos.
     */

    static void setPos(QPoint const& pos);
    static void setPos(int x, int y);

    static xcb_cursor_t x11Cursor(cursor_shape shape);

    /**
     * Notice: if available always use the cursor_shape variant to avoid cache duplicates for
     * ambiguous cursor names in the non existing cursor name specification
     */
    static xcb_cursor_t x11Cursor(QByteArray const& name);

Q_SIGNALS:
    void posChanged(QPoint pos);
    void mouseChanged(QPoint const& pos,
                      QPoint const& oldpos,
                      Qt::MouseButtons buttons,
                      Qt::MouseButtons oldbuttons,
                      Qt::KeyboardModifiers modifiers,
                      Qt::KeyboardModifiers oldmodifiers);

    /**
     * @brief Signal emitted when the cursor image changes.
     *
     * To enable these signals use startCursorTracking.
     *
     * @see startCursorTracking
     * @see stopCursorTracking
     */
    void cursorChanged();
    void themeChanged();

protected:
    /**
     * Called from x11Cursor to actually retrieve the X11 cursor. Base implementation returns
     * a null cursor, an implementing subclass should implement this method if it can provide X11
     * mouse cursors.
     */
    virtual xcb_cursor_t getX11Cursor(cursor_shape shape);

    /**
     * Called from x11Cursor to actually retrieve the X11 cursor. Base implementation returns
     * a null cursor, an implementing subclass should implement this method if it can provide X11
     * mouse cursors.
     */
    virtual xcb_cursor_t getX11Cursor(QByteArray const& name);

    /**
     * Performs the actual warping of the cursor.
     */
    virtual void doSetPos();

    /**
     * Called from @ref pos() to allow syncing the internal position with the underlying
     * system's cursor position.
     */
    virtual void doGetPos();

    /**
     * Called from startMousePolling when the mouse polling gets activated. Base implementation
     * does nothing, inheriting classes can overwrite to e.g. start a timer.
     */
    virtual void doStartMousePolling();

    /**
     * Called from stopMousePolling when the mouse polling gets deactivated. Base implementation
     * does nothing, inheriting classes can overwrite to e.g. stop a timer.
     */
    virtual void doStopMousePolling();

    /**
     * Called from startCursorTracking when cursor image tracking gets activated. Inheriting class
     * needs to overwrite to enable platform specific code for the tracking.
     */
    virtual void doStartCursorTracking();

    /**
     * Called from stopCursorTracking when cursor image tracking gets deactivated. Inheriting class
     * needs to overwrite to disable platform specific code for the tracking.
     */
    virtual void doStopCursorTracking();
    bool isCursorTracking() const;

    /**
     * Provides the actual internal cursor position to inheriting classes. If an inheriting class
     * needs access to the cursor position this method should be used instead of the static @ref
     * pos, as the static method syncs with the underlying system's cursor.
     */
    QPoint const& currentPos() const;

    /**
     * Updates the internal position to @p pos without warping the pointer as
     * setPos does.
     */
    void updatePos(QPoint const& pos);
    void updatePos(int x, int y);

private Q_SLOTS:
    void loadThemeSettings();
    void slotKGlobalSettingsNotifyChange(int type, int arg);

private:
    void updateTheme(QString const& name, int size);
    void loadThemeFromKConfig();
    QPoint m_pos;
    int m_mousePollingCounter;
    int m_cursorTrackingCounter;
    QString m_themeName;
    int m_themeSize;

    static cursor* s_self;
};

inline QPoint const& cursor::currentPos() const
{
    return m_pos;
}

inline void cursor::updatePos(int x, int y)
{
    updatePos(QPoint(x, y));
}

inline QString const& cursor::themeName() const
{
    return m_themeName;
}

inline int cursor::themeSize() const
{
    return m_themeSize;
}

inline bool cursor::isCursorTracking() const
{
    return m_cursorTrackingCounter > 0;
}

}
