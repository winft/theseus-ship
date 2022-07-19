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
