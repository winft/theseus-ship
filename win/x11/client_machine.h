/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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

#include "kwin_export.h"

#include <QObject>
#include <memory>
#include <xcb/xcb.h>

// forward declaration
struct addrinfo;
template<typename T>
class QFutureWatcher;

namespace KWin::win::x11
{

class get_addr_info_wrapper : public QObject
{
    Q_OBJECT
public:
    explicit get_addr_info_wrapper(QByteArray const& hostName);
    ~get_addr_info_wrapper() override;

    void resolve();

Q_SIGNALS:
    void local();
    void finished();

private Q_SLOTS:
    void slotResolved();
    void slotOwnAddressResolved();

private:
    void compare();
    bool resolved(QFutureWatcher<int>* watcher);
    bool m_resolving;
    bool m_resolved;
    bool m_ownResolved;
    QByteArray m_hostname;
    addrinfo* m_addressHints;
    addrinfo* m_address;
    addrinfo* m_ownAddress;
    QFutureWatcher<int>* m_watcher;
    QFutureWatcher<int>* m_ownAddressWatcher;
};

class KWIN_EXPORT client_machine : public QObject
{
    Q_OBJECT
public:
    void resolve(xcb_window_t window, xcb_window_t clientLeader);
    QByteArray const& hostname() const;
    bool is_local() const;
    static QByteArray localhost();
    bool is_resolving() const;

Q_SIGNALS:
    void localhostChanged();

private Q_SLOTS:
    void set_local();
    void resolve_finished();

private:
    void check_for_localhost();

    QByteArray m_hostname;
    std::unique_ptr<get_addr_info_wrapper> resolver;
    bool m_localhost{false};
    bool m_resolved{false};
};

inline bool client_machine::is_local() const
{
    return m_localhost;
}

inline const QByteArray& client_machine::hostname() const
{
    return m_hostname;
}

inline QByteArray client_machine::localhost()
{
    return "localhost";
}

inline bool client_machine::is_resolving() const
{
    return static_cast<bool>(resolver);
}

}
