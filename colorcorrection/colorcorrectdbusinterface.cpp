/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2017 Roman Gilg <subdiff@gmail.com>

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
#include "colorcorrectdbusinterface.h"

#include "colorcorrectadaptor.h"

#include "manager.h"

#include <QDBusMessage>

namespace KWin::ColorCorrect
{

ColorCorrectDBusInterface::ColorCorrectDBusInterface(Manager* manager)
    : m_manager(manager)
    , m_inhibitorWatcher(new QDBusServiceWatcher(this))
{
    m_inhibitorWatcher->setConnection(QDBusConnection::sessionBus());
    m_inhibitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_inhibitorWatcher,
            &QDBusServiceWatcher::serviceUnregistered,
            this,
            &ColorCorrectDBusInterface::removeInhibitorService);

    connect(m_manager, &Manager::inhibited_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("inhibited"), m_manager->is_inhibited());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::enabled_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("enabled"), m_manager->is_enabled());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::runningChanged, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("running"), m_manager->is_running());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::current_temperature_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("currentTemperature"),
                                 m_manager->current_temperature());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::target_temperature_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("targetTemperature"),
                                 m_manager->get_target_temperature());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::mode_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("mode"), uint(m_manager->mode()));

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::previous_transition_timings_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("previousTransitionDateTime"),
                                 previousTransitionDateTime());
        changedProperties.insert(QStringLiteral("previousTransitionDuration"),
                                 previousTransitionDuration());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    connect(m_manager, &Manager::scheduled_transition_timings_changed, this, [this] {
        QVariantMap changedProperties;
        changedProperties.insert(QStringLiteral("scheduledTransitionDateTime"),
                                 scheduledTransitionDateTime());
        changedProperties.insert(QStringLiteral("scheduledTransitionDuration"),
                                 scheduledTransitionDuration());

        QDBusMessage message
            = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                         QStringLiteral("PropertiesChanged"));

        message.setArguments({
            QStringLiteral("org.kde.kwin.ColorCorrect"),
            changedProperties,
            QStringList(), // invalidated_properties
        });

        QDBusConnection::sessionBus().send(message);
    });

    new ColorCorrectAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/ColorCorrect"), this);
}

bool ColorCorrectDBusInterface::isInhibited() const
{
    return m_manager->is_inhibited();
}

bool ColorCorrectDBusInterface::isEnabled() const
{
    return m_manager->is_enabled();
}

bool ColorCorrectDBusInterface::isRunning() const
{
    return m_manager->is_running();
}

bool ColorCorrectDBusInterface::isAvailable() const
{
    return m_manager->is_available();
}

int ColorCorrectDBusInterface::currentTemperature() const
{
    return m_manager->current_temperature();
}

int ColorCorrectDBusInterface::targetTemperature() const
{
    return m_manager->get_target_temperature();
}

int ColorCorrectDBusInterface::mode() const
{
    return m_manager->mode();
}

quint64 ColorCorrectDBusInterface::previousTransitionDateTime() const
{
    auto const dateTime = m_manager->previous_transition_date_time();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 ColorCorrectDBusInterface::previousTransitionDuration() const
{
    return quint32(m_manager->previous_transition_duration());
}

quint64 ColorCorrectDBusInterface::scheduledTransitionDateTime() const
{
    const QDateTime dateTime = m_manager->scheduled_transition_date_time();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 ColorCorrectDBusInterface::scheduledTransitionDuration() const
{
    return quint32(m_manager->scheduled_transition_duration());
}

void ColorCorrectDBusInterface::nightColorAutoLocationUpdate(double latitude, double longitude)
{
    m_manager->auto_location_update(latitude, longitude);
}

uint ColorCorrectDBusInterface::inhibit()
{
    const QString serviceName = QDBusContext::message().service();

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->addWatchedService(serviceName);
    }

    m_inhibitors.insert(serviceName, ++m_lastInhibitionCookie);

    m_manager->inhibit();

    return m_lastInhibitionCookie;
}

void ColorCorrectDBusInterface::uninhibit(uint cookie)
{
    const QString serviceName = QDBusContext::message().service();

    uninhibit(serviceName, cookie);
}

void ColorCorrectDBusInterface::uninhibit(const QString& serviceName, uint cookie)
{
    const int removedCount = m_inhibitors.remove(serviceName, cookie);
    if (!removedCount) {
        return;
    }

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->removeWatchedService(serviceName);
    }

    m_manager->uninhibit();
}

void ColorCorrectDBusInterface::removeInhibitorService(const QString& serviceName)
{
    const auto cookies = m_inhibitors.values(serviceName);
    for (const uint& cookie : cookies) {
        uninhibit(serviceName, cookie);
    }
}

}
