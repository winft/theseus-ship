/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "redirect.h"

#include <QInputEvent>

namespace KWin::input
{
class keyboard;
class pointer;
class switch_device;
class touch;

template<typename Device>
struct event {
    Device* dev{nullptr};
    uint32_t time_msec;
};

/** Pointer events */

enum class axis_orientation {
    vertical,
    horizontal,
};

enum class axis_source {
    unknown,
    wheel,
    finger,
    continuous,
    wheel_tilt,
};

enum class button_state {
    released,
    pressed,
};

struct button_event {
    uint32_t key;
    button_state state;
    event<pointer> base;
};

struct motion_event {
    QPointF delta;
    QPointF unaccel_delta;
    event<pointer> base;
};

struct motion_absolute_event {
    QPointF pos;
    event<pointer> base;
};

struct axis_event {
    axis_source source;
    axis_orientation orientation;
    double delta;
    int32_t delta_discrete;
    event<pointer> base;
};

struct swipe_begin_event {
    uint32_t fingers;
    event<pointer> base;
};

struct swipe_update_event {
    uint32_t fingers;
    QPointF delta;
    event<pointer> base;
};

struct swipe_end_event {
    bool cancelled{false};
    event<pointer> base;
};

struct pinch_begin_event {
    uint32_t fingers;
    event<pointer> base;
};

struct pinch_update_event {
    uint32_t fingers;
    QPointF delta;
    double scale;
    double rotation;
    event<pointer> base;
};

struct pinch_end_event {
    bool cancelled{false};
    event<pointer> base;
};

/** Keyboard events */

enum class keyboard_led {
    num_lock,
    caps_lock,
    scroll_lock,
};

enum class modifier {
    shift,
    caps,
    ctrl,
    alt,
    mod2,
    mod3,
    logo,
    mod5,
};

struct key_event {
    uint32_t keycode;
    button_state state;
    bool requires_modifier_update;
    event<keyboard> base;
};

struct modifiers_event {
    uint32_t depressed;
    uint32_t latched;
    uint32_t locked;
    uint32_t group;
    struct {
        keyboard* dev;
    } base;
};

/** Touch events */

struct touch_down_event {
    int32_t id;
    QPointF pos;
    event<touch> base;
};

struct touch_up_event {
    int32_t id;
    event<touch> base;
};

struct touch_motion_event {
    int32_t id;
    QPointF pos;
    event<touch> base;
};

struct touch_cancel_event {
    int32_t id;
    event<touch> base;
};

/** Switch events */

enum class switch_type {
    lid = 1,
    tablet_mode,
};

enum class switch_state {
    off = 0,
    on,
    toggle,
};

struct toggle_event {
    switch_type type;
    switch_state state;
    event<switch_device> base;
};

class MouseEvent : public QMouseEvent
{
public:
    explicit MouseEvent(QEvent::Type type,
                        const QPointF& pos,
                        Qt::MouseButton button,
                        Qt::MouseButtons buttons,
                        Qt::KeyboardModifiers modifiers,
                        quint32 timestamp,
                        const QSizeF& delta,
                        const QSizeF& deltaNonAccelerated,
                        quint64 timestampMicroseconds,
                        pointer* device);

    QSizeF delta() const
    {
        return m_delta;
    }

    QSizeF deltaUnaccelerated() const
    {
        return m_deltaUnccelerated;
    }

    quint64 timestampMicroseconds() const
    {
        return m_timestampMicroseconds;
    }

    pointer* device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers& mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

    quint32 nativeButton() const
    {
        return m_nativeButton;
    }

    void setNativeButton(quint32 button)
    {
        m_nativeButton = button;
    }

private:
    QSizeF m_delta;
    QSizeF m_deltaUnccelerated;
    quint64 m_timestampMicroseconds;
    pointer* m_device;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
    quint32 m_nativeButton = 0;
};

// TODO: Don't derive from QWheelEvent, this event is quite domain specific.
class WheelEvent : public QWheelEvent
{
public:
    explicit WheelEvent(const QPointF& pos,
                        qreal delta,
                        qint32 discreteDelta,
                        Qt::Orientation orientation,
                        Qt::MouseButtons buttons,
                        Qt::KeyboardModifiers modifiers,
                        redirect::PointerAxisSource source,
                        quint32 timestamp,
                        pointer* device);

    Qt::Orientation orientation() const
    {
        return m_orientation;
    }

    qreal delta() const
    {
        return m_delta;
    }

    qint32 discreteDelta() const
    {
        return m_discreteDelta;
    }

    redirect::PointerAxisSource axisSource() const
    {
        return m_source;
    }

    pointer* device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers& mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

private:
    pointer* m_device;
    Qt::Orientation m_orientation;
    qreal m_delta;
    qint32 m_discreteDelta;
    redirect::PointerAxisSource m_source;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
};

class KeyEvent : public QKeyEvent
{
public:
    explicit KeyEvent(QEvent::Type type,
                      Qt::Key key,
                      Qt::KeyboardModifiers modifiers,
                      quint32 code,
                      quint32 keysym,
                      const QString& text,
                      bool autorepeat,
                      quint32 timestamp,
                      keyboard* device);

    keyboard* device() const
    {
        return m_device;
    }

    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const
    {
        return m_modifiersRelevantForShortcuts;
    }

    void setModifiersRelevantForGlobalShortcuts(const Qt::KeyboardModifiers& mods)
    {
        m_modifiersRelevantForShortcuts = mods;
    }

private:
    keyboard* m_device;
    Qt::KeyboardModifiers m_modifiersRelevantForShortcuts = Qt::KeyboardModifiers();
};

class SwitchEvent : public QInputEvent
{
public:
    enum class State { Off, On };
    explicit SwitchEvent(State state,
                         quint32 timestamp,
                         quint64 timestampMicroseconds,
                         switch_device* device);

    State state() const
    {
        return m_state;
    }

    quint64 timestampMicroseconds() const
    {
        return m_timestampMicroseconds;
    }

    switch_device* device() const
    {
        return m_device;
    }

private:
    State m_state;
    quint64 m_timestampMicroseconds;
    switch_device* m_device;
};

}
