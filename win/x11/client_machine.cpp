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
#include "client_machine.h"

#include "base/logging.h"
#include "main.h"

#include <NETWM>

#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace KWin::win::x11
{

static QByteArray get_hostname_helper()
{
#ifdef HOST_NAME_MAX
    char hostnamebuf[HOST_NAME_MAX];
#else
    char hostnamebuf[256];
#endif
    if (gethostname(hostnamebuf, sizeof hostnamebuf) >= 0) {
        hostnamebuf[sizeof(hostnamebuf) - 1] = 0;
        return QByteArray(hostnamebuf);
    }
    return QByteArray();
}

get_addr_info_wrapper::get_addr_info_wrapper(QByteArray const& hostName)
    : m_resolving(false)
    , m_resolved(false)
    , m_ownResolved(false)
    , m_hostname(hostName)
    , m_addressHints(new addrinfo)
    , m_address(nullptr)
    , m_ownAddress(nullptr)
    , m_watcher(new QFutureWatcher<int>(this))
    , m_ownAddressWatcher(new QFutureWatcher<int>(this))
{
    // Watcher will be deleted together with the get_addr_info_wrapper once the future got canceled
    // or finished.
    connect(m_watcher, &QFutureWatcher<int>::canceled, this, &get_addr_info_wrapper::finished);
    connect(m_watcher, &QFutureWatcher<int>::finished, this, &get_addr_info_wrapper::slotResolved);
    connect(m_ownAddressWatcher,
            &QFutureWatcher<int>::canceled,
            this,
            &get_addr_info_wrapper::finished);
    connect(m_ownAddressWatcher,
            &QFutureWatcher<int>::finished,
            this,
            &get_addr_info_wrapper::slotOwnAddressResolved);
}

get_addr_info_wrapper::~get_addr_info_wrapper()
{
    if (m_watcher && m_watcher->isRunning()) {
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
    if (m_ownAddressWatcher && m_ownAddressWatcher->isRunning()) {
        m_ownAddressWatcher->cancel();
        m_ownAddressWatcher->waitForFinished();
    }
    if (m_address) {
        freeaddrinfo(m_address);
    }
    if (m_ownAddress) {
        freeaddrinfo(m_ownAddress);
    }
    delete m_addressHints;
}

void get_addr_info_wrapper::resolve()
{
    if (m_resolving) {
        return;
    }
    m_resolving = true;
    memset(m_addressHints, 0, sizeof(*m_addressHints));
    m_addressHints->ai_family = PF_UNSPEC;
    m_addressHints->ai_socktype = SOCK_STREAM;
    m_addressHints->ai_flags |= AI_CANONNAME;

    m_watcher->setFuture(QtConcurrent::run(
        getaddrinfo, m_hostname.constData(), nullptr, m_addressHints, &m_address));
    m_ownAddressWatcher->setFuture(QtConcurrent::run([this] {
        // needs to be performed in a lambda as get_hostname_helper() returns a temporary value
        // which would get destroyed in the main thread before the getaddrinfo thread is able to
        // read it
        return getaddrinfo(
            get_hostname_helper().constData(), nullptr, m_addressHints, &m_ownAddress);
    }));
}

void get_addr_info_wrapper::slotResolved()
{
    if (resolved(m_watcher)) {
        m_resolved = true;
        compare();
    }
}

void get_addr_info_wrapper::slotOwnAddressResolved()
{
    if (resolved(m_ownAddressWatcher)) {
        m_ownResolved = true;
        compare();
    }
}

bool get_addr_info_wrapper::resolved(QFutureWatcher<int>* watcher)
{
    if (!watcher->isFinished()) {
        return false;
    }
    if (watcher->result() != 0) {
        qCDebug(KWIN_CORE) << "getaddrinfo failed with error:" << gai_strerror(watcher->result());
        // call failed;
        Q_EMIT finished();
        return false;
    }
    return true;
}

void get_addr_info_wrapper::compare()
{
    if (!m_resolved || !m_ownResolved) {
        return;
    }
    addrinfo* address = m_address;
    while (address) {
        if (address->ai_canonname && m_hostname == QByteArray(address->ai_canonname).toLower()) {
            addrinfo* ownAddress = m_ownAddress;
            bool localFound = false;
            while (ownAddress) {
                if (ownAddress->ai_canonname
                    && QByteArray(ownAddress->ai_canonname).toLower() == m_hostname) {
                    localFound = true;
                    break;
                }
                ownAddress = ownAddress->ai_next;
            }
            if (localFound) {
                Q_EMIT local();
                break;
            }
        }
        address = address->ai_next;
    }
    Q_EMIT finished();
}

client_machine::client_machine(QObject* parent)
    : QObject(parent)
    , m_localhost(false)
    , m_resolved(false)
{
}

client_machine::~client_machine()
{
}

void client_machine::resolve(xcb_window_t window, xcb_window_t clientLeader)
{
    if (m_resolved) {
        return;
    }
    QByteArray name
        = NETWinInfo(connection(), window, rootWindow(), NET::Properties(), NET::WM2ClientMachine)
              .clientMachine();
    if (name.isEmpty() && clientLeader && clientLeader != window) {
        name = NETWinInfo(connection(),
                          clientLeader,
                          rootWindow(),
                          NET::Properties(),
                          NET::WM2ClientMachine)
                   .clientMachine();
    }
    if (name.isEmpty()) {
        name = localhost();
    }
    if (name == localhost()) {
        set_local();
    }
    m_hostname = name;
    check_for_localhost();
    m_resolved = true;
}

void client_machine::check_for_localhost()
{
    if (is_local()) {
        // nothing to do
        return;
    }
    auto host = get_hostname_helper();

    if (!host.isEmpty()) {
        host = host.toLower();
        const QByteArray lowerHostName(m_hostname.toLower());
        if (host == lowerHostName) {
            set_local();
            return;
        }
        if (char* dot = strchr(host.data(), '.')) {
            *dot = '\0';
            if (host == lowerHostName) {
                set_local();
                return;
            }
        } else {
            // check using information from get addr info
            // get_addr_info_wrapper gets automatically destroyed once it finished or not
            resolver = std::make_unique<get_addr_info_wrapper>(lowerHostName);
            connect(
                resolver.get(), &get_addr_info_wrapper::local, this, &client_machine::set_local);
            connect(resolver.get(),
                    &get_addr_info_wrapper::finished,
                    this,
                    &client_machine::resolve_finished);
            resolver->resolve();
        }
    }
}

void client_machine::set_local()
{
    m_localhost = true;
    Q_EMIT localhostChanged();
}

void client_machine::resolve_finished()
{
    resolver.reset();
}

} // namespace
