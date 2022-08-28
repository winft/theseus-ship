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

#include <QDBusServiceWatcher>

namespace KWin::base::dbus
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

    if (!dbus.registerService(m_serviceName)) {
        QDBusServiceWatcher* dog = new QDBusServiceWatcher(
            m_serviceName, dbus, QDBusServiceWatcher::WatchForUnregistration, this);
        connect(dog, &QDBusServiceWatcher::serviceUnregistered, this, &kwin::becomeKWinService);
    }

    dbus.connect(QString(),
                 QStringLiteral("/KWin"),
                 QStringLiteral("org.kde.KWin"),
                 QStringLiteral("reloadConfig"),
                 &space,
                 SLOT(reconfigure()));
}

void kwin::becomeKWinService(const QString& service)
{
    // TODO: this watchdog exists to make really safe that we at some point get the service
    // but it's probably no longer needed since we explicitly unregister the service with the
    // deconstructor
    if (service == m_serviceName && QDBusConnection::sessionBus().registerService(m_serviceName)
        && sender()) {
        sender()->deleteLater();
    }
}

kwin::~kwin()
{
    QDBusConnection::sessionBus().unregisterService(m_serviceName);

    // KApplication automatically also grabs org.kde.kwin, so it's often been used externally -
    // ensure to free it as well
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.kwin"));
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
