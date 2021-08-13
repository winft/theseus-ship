/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cursor_redirect.h"

#include "../platform.h"
#include "main.h"
#include "utils.h"
#include "xcbutils.h"
#include <kwinglobals.h>

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QAbstractEventDispatcher>
#include <QDBusConnection>
#include <QScreen>
#include <QTimer>

namespace KWin::input
{

cursor_redirect::cursor_redirect()
    : cursor()
    , m_currentButtons(Qt::NoButton)
{
    connect(kwinApp()->input_redirect.get(),
            SIGNAL(globalPointerChanged(QPointF)),
            SLOT(slotPosChanged(QPointF)));
    connect(kwinApp()->input_redirect.get(),
            SIGNAL(pointerButtonStateChanged(uint32_t, input::redirect::PointerButtonState)),
            SLOT(slotPointerButtonChanged()));
#ifndef KCMRULES
    connect(kwinApp()->input_redirect.get(),
            &input::redirect::keyboardModifiersChanged,
            this,
            &cursor_redirect::slotModifiersChanged);
#endif
}

cursor_redirect::~cursor_redirect()
{
}

void cursor_redirect::doSetPos()
{
    if (kwinApp()->input_redirect->supportsPointerWarping()) {
        kwinApp()->input_redirect->warpPointer(currentPos());
    }
    slotPosChanged(kwinApp()->input_redirect->globalPointer());
    emit posChanged(currentPos());
}

void cursor_redirect::slotPosChanged(const QPointF& pos)
{
    const QPoint oldPos = currentPos();
    updatePos(pos.toPoint());
    emit mouseChanged(pos.toPoint(),
                      oldPos,
                      m_currentButtons,
                      m_currentButtons,
                      kwinApp()->input_redirect->keyboardModifiers(),
                      kwinApp()->input_redirect->keyboardModifiers());
}

void cursor_redirect::slotModifiersChanged(Qt::KeyboardModifiers mods,
                                           Qt::KeyboardModifiers oldMods)
{
    emit mouseChanged(
        currentPos(), currentPos(), m_currentButtons, m_currentButtons, mods, oldMods);
}

void cursor_redirect::slotPointerButtonChanged()
{
    const Qt::MouseButtons oldButtons = m_currentButtons;
    m_currentButtons = kwinApp()->input_redirect->qtButtonStates();
    const QPoint pos = currentPos();
    emit mouseChanged(pos,
                      pos,
                      m_currentButtons,
                      oldButtons,
                      kwinApp()->input_redirect->keyboardModifiers(),
                      kwinApp()->input_redirect->keyboardModifiers());
}

void cursor_redirect::doStartCursorTracking()
{
#ifndef KCMRULES
    connect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::cursorChanged);
#endif
}

void cursor_redirect::doStopCursorTracking()
{
#ifndef KCMRULES
    disconnect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::cursorChanged);
#endif
}

}
