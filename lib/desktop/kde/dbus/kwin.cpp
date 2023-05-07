/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin.h"

#include "kwinadaptor.h"

#include "debug/console/console.h"
#include "debug/perf/ftrace.h"
#include "win/space_qobject.h"

namespace KWin::desktop::kde
{

kwin::kwin(win::space_qobject& space)
    : m_serviceName(QStringLiteral("org.kde.KWin"))
    , space{space}
{
    (void)new KWinAdaptor(this);

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/KWin"), this);

    auto const dBusSuffix = qgetenv("KWIN_DBUS_SERVICE_SUFFIX");
    if (!dBusSuffix.isNull()) {
        m_serviceName = m_serviceName + QLatin1Char('.') + dBusSuffix;
    }

    dbus.registerService(m_serviceName);
    dbus.connect(QString(),
                 QStringLiteral("/KWin"),
                 QStringLiteral("org.kde.KWin"),
                 QStringLiteral("reloadConfig"),
                 &space,
                 SLOT(reconfigure()));
}

kwin::~kwin()
{
    QDBusConnection::sessionBus().unregisterService(m_serviceName);
}

void kwin::showDesktop(bool show)
{
    show_desktop_impl(show);

    auto msg = message();
    if (msg.service().isEmpty()) {
        return;
    }

    // Keep track of whatever D-Bus client asked to show the desktop. If
    // they disappear from the bus, cancel the show desktop state so we do
    // not end up in a state where we are stuck showing the desktop.
    static QPointer<QDBusServiceWatcher> watcher;

    if (show) {
        if (watcher) {
            // If we get a second call to `showDesktop(true)`, drop the previous
            // watcher and watch the new client. That way, we simply always
            // track the last state.
            watcher->deleteLater();
        }

        watcher = new QDBusServiceWatcher(msg.service(),
                                          QDBusConnection::sessionBus(),
                                          QDBusServiceWatcher::WatchForUnregistration,
                                          this);
        connect(watcher, &QDBusServiceWatcher::serviceUnregistered, [this] {
            show_desktop_impl(false);
            watcher->deleteLater();
        });
    } else if (watcher) {
        // Someone cancelled showing the desktop, so there's no more need to
        // watch to cancel the show desktop state.
        watcher->deleteLater();
    }
}

void kwin::reconfigure()
{
    space.reconfigure();
}

bool kwin::startActivity(const QString& /*in0*/)
{
    return false;
}

bool kwin::stopActivity(const QString& /*in0*/)
{
    return false;
}

void kwin::enableFtrace(bool enable)
{
    if (Perf::Ftrace::setEnabled(enable)) {
        return;
    }

    // Operation failed. Send error reply.
    auto const msg
        = QStringLiteral("Ftrace marker could not be ").append(enable ? "enabled" : "disabled");
    QDBusConnection::sessionBus().send(
        message().createErrorReply("org.kde.KWin.enableFtrace", msg));
}

}
