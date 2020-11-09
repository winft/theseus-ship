/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>
Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include <QObject>
#include <QVector>
#include <QMap>

namespace Wrapland
{
namespace Server
{
class KdeIdle;
}
}

using Wrapland::Server::KdeIdle;

namespace KWin
{
class Toplevel;
class XdgShellClient;

class IdleInhibition : public QObject
{
    Q_OBJECT
public:
    explicit IdleInhibition(KdeIdle *idle);
    ~IdleInhibition() override;

    void registerXdgShellClient(XdgShellClient *client);

    bool isInhibited() const {
        return !m_idleInhibitors.isEmpty();
    }
    bool isInhibited(Toplevel* window) const {
        return m_idleInhibitors.contains(window);
    }

private Q_SLOTS:
    void slotWorkspaceCreated();
    void slotDesktopChanged();

private:
    void inhibit(Toplevel* window);
    void uninhibit(Toplevel* window);
    void update(Toplevel* window);

    KdeIdle *m_idle;
    QVector<Toplevel*> m_idleInhibitors;
    QMap<Toplevel*, QMetaObject::Connection> m_connections;
};
}
