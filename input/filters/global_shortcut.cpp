/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcut.h"

#include "globalshortcuts.h"
#include "input/event.h"
#include "main.h"

#include <QKeyEvent>
#include <QTimer>

namespace KWin::input
{

global_shortcut_filter::global_shortcut_filter()
{
    m_powerDown = new QTimer;
    m_powerDown->setSingleShot(true);
    m_powerDown->setInterval(1000);
}
global_shortcut_filter::~global_shortcut_filter()
{
    delete m_powerDown;
}

bool global_shortcut_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton);
    if (event->type() == QEvent::MouseButtonPress) {
        if (kwinApp()->input_redirect->shortcuts()->processPointerPressed(event->modifiers(),
                                                                          event->buttons())) {
            return true;
        }
    }
    return false;
}

bool global_shortcut_filter::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() == Qt::NoModifier) {
        return false;
    }
    PointerAxisDirection direction = PointerAxisUp;
    if (event->angleDelta().x() < 0) {
        direction = PointerAxisRight;
    } else if (event->angleDelta().x() > 0) {
        direction = PointerAxisLeft;
    } else if (event->angleDelta().y() < 0) {
        direction = PointerAxisDown;
    } else if (event->angleDelta().y() > 0) {
        direction = PointerAxisUp;
    }
    return kwinApp()->input_redirect->shortcuts()->processAxis(event->modifiers(), direction);
}

bool global_shortcut_filter::keyEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_PowerOff) {
        const auto modifiers = static_cast<KeyEvent*>(event)->modifiersRelevantForGlobalShortcuts();
        if (event->type() == QEvent::KeyPress && !event->isAutoRepeat()) {
            QObject::connect(m_powerDown,
                             &QTimer::timeout,
                             kwinApp()->input_redirect->shortcuts(),
                             [this, modifiers] {
                                 QObject::disconnect(m_powerDown,
                                                     &QTimer::timeout,
                                                     kwinApp()->input_redirect->shortcuts(),
                                                     nullptr);
                                 m_powerDown->stop();
                                 kwinApp()->input_redirect->shortcuts()->processKey(
                                     modifiers, Qt::Key_PowerDown);
                             });
            m_powerDown->start();
            return true;
        } else if (event->type() == QEvent::KeyRelease) {
            const bool ret = !m_powerDown->isActive()
                || kwinApp()->input_redirect->shortcuts()->processKey(modifiers, event->key());
            m_powerDown->stop();
            return ret;
        }
    } else if (event->type() == QEvent::KeyPress) {
        return kwinApp()->input_redirect->shortcuts()->processKey(
            static_cast<KeyEvent*>(event)->modifiersRelevantForGlobalShortcuts(), event->key());
    }
    return false;
}

bool global_shortcut_filter::swipeGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input_redirect->shortcuts()->processSwipeStart(fingerCount);
    return false;
}

bool global_shortcut_filter::swipeGestureUpdate(QSizeF const& delta, quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input_redirect->shortcuts()->processSwipeUpdate(delta);
    return false;
}

bool global_shortcut_filter::swipeGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input_redirect->shortcuts()->processSwipeCancel();
    return false;
}

bool global_shortcut_filter::swipeGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input_redirect->shortcuts()->processSwipeEnd();
    return false;
}

}
