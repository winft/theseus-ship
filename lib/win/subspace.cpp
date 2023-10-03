/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace.h"

namespace KWin::win
{

subspace::subspace(QObject* parent)
    : QObject(parent)
{
}

subspace::~subspace()
{
    Q_EMIT aboutToBeDestroyed();
}

void subspace::setId(QString const& id)
{
    Q_ASSERT(m_id.isEmpty());
    assert(!id.isEmpty());
    m_id = id;
}

void subspace::setX11DesktopNumber(uint number)
{
    // x11DesktopNumber can be changed now
    if (static_cast<uint>(m_x11DesktopNumber) == number) {
        return;
    }

    m_x11DesktopNumber = number;

    if (m_x11DesktopNumber != 0) {
        Q_EMIT x11DesktopNumberChanged();
    }
}

void subspace::setName(QString const& name)
{
    if (m_name == name) {
        return;
    }

    m_name = name;
    Q_EMIT nameChanged();
}

}
