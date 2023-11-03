/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace.h"

#include <QUuid>

namespace KWin::win
{

static QString generateDesktopId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

subspace::subspace(QObject* parent)
    : subspace(generateDesktopId(), parent)
{
}

subspace::subspace(QString const& id, QObject* parent)
    : QObject(parent)
    , m_id{id}
{
    if (id.isEmpty()) {
        m_id = generateDesktopId();
    }
}

subspace::~subspace()
{
    Q_EMIT aboutToBeDestroyed();
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
