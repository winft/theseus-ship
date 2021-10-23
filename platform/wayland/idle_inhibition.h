/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QMap>
#include <QObject>
#include <QVector>

namespace Wrapland::Server
{
class KdeIdle;
}

using Wrapland::Server::KdeIdle;

namespace KWin
{

namespace win::wayland
{
class window;
}

class Toplevel;
class XdgShellClient;

namespace platform::wayland
{

class idle_inhibition : public QObject
{
    Q_OBJECT
public:
    explicit idle_inhibition(KdeIdle* idle);
    ~idle_inhibition() override;

    void register_window(win::wayland::window* window);

    bool isInhibited() const
    {
        return !m_idleInhibitors.isEmpty();
    }
    bool isInhibited(Toplevel* window) const
    {
        return m_idleInhibitors.contains(window);
    }

private Q_SLOTS:
    void slotWorkspaceCreated();
    void slotDesktopChanged();

private:
    void inhibit(Toplevel* window);
    void uninhibit(Toplevel* window);
    void update(Toplevel* window);

    KdeIdle* m_idle;
    QVector<Toplevel*> m_idleInhibitors;
    QMap<Toplevel*, QMetaObject::Connection> m_connections;
};

}
}
