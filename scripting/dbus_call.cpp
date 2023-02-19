/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "dbus_call.h"

#include "utils.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

namespace KWin::scripting
{

dbus_call::dbus_call(QObject* parent)
    : QObject(parent)
{
}

dbus_call::~dbus_call()
{
}

void dbus_call::call()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(m_service, m_path, m_interface, m_method);
    msg.setArguments(m_arguments);

    QDBusPendingCallWatcher* watcher
        = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, [this, watcher]() {
        watcher->deleteLater();
        if (watcher->isError()) {
            Q_EMIT failed();
            return;
        }
        QVariantList reply = watcher->reply().arguments();
        std::for_each(reply.begin(), reply.end(), [](QVariant& variant) {
            variant = dbusToVariant(variant);
        });
        Q_EMIT finished(reply);
    });
}

} // KWin
