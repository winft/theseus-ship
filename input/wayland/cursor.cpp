/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cursor.h"

#include "main.h"
#include "utils.h"
#include "xcbutils.h"
#include <kwinglobals.h>
#include <platform.h>

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QAbstractEventDispatcher>
#include <QDBusConnection>
#include <QScreen>
#include <QTimer>

namespace KWin::input::wayland
{

cursor::cursor()
    : input::cursor()
    , m_currentButtons(Qt::NoButton)
{
    connect(kwinApp()->input->redirect.get(),
            SIGNAL(globalPointerChanged(QPointF)),
            SLOT(slotPosChanged(QPointF)));
    connect(kwinApp()->input->redirect.get(),
            SIGNAL(pointerButtonStateChanged(uint32_t, input::redirect::PointerButtonState)),
            SLOT(slotPointerButtonChanged()));

#ifndef KCMRULES
    connect(kwinApp()->input->redirect.get(),
            &input::redirect::keyboardModifiersChanged,
            this,
            &cursor::slotModifiersChanged);
#endif
}

void cursor::doSetPos()
{
    if (kwinApp()->input->redirect->supportsPointerWarping()) {
        kwinApp()->input->redirect->warpPointer(currentPos());
    }
    slotPosChanged(kwinApp()->input->redirect->globalPointer());
    Q_EMIT posChanged(currentPos());
}

void cursor::slotPosChanged(const QPointF& pos)
{
    auto const oldPos = currentPos();
    updatePos(pos.toPoint());
    Q_EMIT mouseChanged(pos.toPoint(),
                        oldPos,
                        m_currentButtons,
                        m_currentButtons,
                        kwinApp()->input->redirect->keyboardModifiers(),
                        kwinApp()->input->redirect->keyboardModifiers());
}

void cursor::slotModifiersChanged(Qt::KeyboardModifiers mods, Qt::KeyboardModifiers oldMods)
{
    Q_EMIT mouseChanged(
        currentPos(), currentPos(), m_currentButtons, m_currentButtons, mods, oldMods);
}

void cursor::slotPointerButtonChanged()
{
    Qt::MouseButtons const oldButtons = m_currentButtons;
    m_currentButtons = kwinApp()->input->redirect->qtButtonStates();
    auto const pos = currentPos();
    Q_EMIT mouseChanged(pos,
                        pos,
                        m_currentButtons,
                        oldButtons,
                        kwinApp()->input->redirect->keyboardModifiers(),
                        kwinApp()->input->redirect->keyboardModifiers());
}

void cursor::doStartCursorTracking()
{
#ifndef KCMRULES
    connect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::cursorChanged);
#endif
}

void cursor::doStopCursorTracking()
{
#ifndef KCMRULES
    disconnect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::cursorChanged);
#endif
}

}
