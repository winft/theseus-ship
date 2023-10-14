/*
    SPDX-FileCopyrightText: 2020 Méven Car <meven.car@enika.com>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KApplicationTrader>
#include <QFileInfo>
#include <QLoggingCategory>

namespace KWin::desktop::kde
{

static QString const s_waylandInterfaceName = QStringLiteral("X-KDE-Wayland-Interfaces");
static QString const s_dbusRestrictedInterfaceName
    = QStringLiteral("X-KDE-DBUS-Restricted-Interfaces");

static QStringList fetchProcessServiceField(const QString& executablePath, const QString& fieldName)
{
    // needed to be able to use the logging category in a header static function
    static QLoggingCategory KWIN_UTILS("KWIN_UTILS", QtWarningMsg);

    auto const servicesFound
        = KApplicationTrader::query([&executablePath](const KService::Ptr& service) {
              if (service->exec().isEmpty()
                  || QFileInfo(service->exec()).canonicalFilePath() != executablePath) {
                  return false;
              }
              return true;
          });

    if (servicesFound.isEmpty()) {
        qCDebug(KWIN_UTILS) << "Could not find the desktop file for" << executablePath;
        return {};
    }

    auto const fieldValues = servicesFound.first()->property<QStringList>(fieldName);
    if (KWIN_UTILS().isDebugEnabled()) {
        qCDebug(KWIN_UTILS) << "Interfaces found for" << executablePath << fieldName << ":"
                            << fieldValues;
    }

    return fieldValues;
}

static inline QStringList fetchRequestedInterfaces(const QString& executablePath)
{
    return fetchProcessServiceField(executablePath, s_waylandInterfaceName);
}

static inline QStringList fetchRestrictedDBusInterfacesFromPid(const uint pid)
{
    auto const executablePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    return fetchProcessServiceField(executablePath, s_dbusRestrictedInterfaceName);
}

}
