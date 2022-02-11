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

#include "rules/rule_book.h"

#include <QDBusConnection>

#include <KConfig>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace KWin::win
{

session_manager::session_manager(QObject* parent)
    : QObject(parent)
{
    new SessionAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Session"), this);
}

session_manager::~session_manager()
{
}

SessionState session_manager::state() const
{
    return m_sessionState;
}

void session_manager::setState(uint state)
{
    switch (state) {
    case 0:
        setState(SessionState::Saving);
        break;
    case 1:
        setState(SessionState::Quitting);
        break;
    default:
        setState(SessionState::Normal);
    }
}

// TODO should we rethink this now that we have dedicated start end end save methods?
void session_manager::setState(SessionState state)
{
    if (state == m_sessionState) {
        return;
    }
    // If we're starting to save a session
    if (state == SessionState::Saving) {
        RuleBook::self()->setUpdatesDisabled(true);
    }
    // If we're ending a save session due to either completion or cancellation
    if (m_sessionState == SessionState::Saving) {
        RuleBook::self()->setUpdatesDisabled(false);
    }
    m_sessionState = state;
    Q_EMIT stateChanged();
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
