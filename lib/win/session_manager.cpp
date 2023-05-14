/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "session_manager.h"

// Include first to not clash with later X definitions in other includes.
#include "sessionadaptor.h"

#include "stacking_order.h"
#include "x11/geo.h"
#include "x11/window.h"

#include <KConfig>
#include <QDBusConnection>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace KWin::win
{

session_manager::session_manager()
{
    new SessionAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Session"), this);
}

session_manager::~session_manager()
{
}

session_state session_manager::state() const
{
    return m_sessionState;
}

void session_manager::setState(uint state)
{
    switch (state) {
    case 0:
        setState(session_state::saving);
        break;
    case 1:
        setState(session_state::quitting);
        break;
    default:
        setState(session_state::normal);
    }
}

// TODO should we rethink this now that we have dedicated start end end save methods?
void session_manager::setState(session_state state)
{
    if (state == m_sessionState) {
        return;
    }

    auto const old_state = m_sessionState;
    m_sessionState = state;

    Q_EMIT stateChanged(old_state, state);
}

void session_manager::loadSession(const QString& name)
{
    Q_EMIT loadSessionRequested(name);
}

void session_manager::aboutToSaveSession(const QString& name)
{
    Q_EMIT prepareSessionSaveRequested(name);
}

void session_manager::finishSaveSession(const QString& name)
{
    Q_EMIT finishSessionSaveRequested(name);
}

void session_manager::quit()
{
    qApp->quit();
}

}
