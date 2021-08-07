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
