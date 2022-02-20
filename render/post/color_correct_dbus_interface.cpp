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
#include "color_correct_dbus_interface.h"

#include "colorcorrectadaptor.h"
#include "night_color_manager.h"

#include <QDBusMessage>

namespace KWin::render::post
{

color_correct_dbus_interface::color_correct_dbus_interface(night_color_manager* manager)
    : m_manager(manager)
    , m_inhibitorWatcher(new QDBusServiceWatcher(this))
{
    m_inhibitorWatcher->setConnection(QDBusConnection::sessionBus());
    m_inhibitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_inhibitorWatcher,
            &QDBusServiceWatcher::serviceUnregistered,
            this,
            &color_correct_dbus_interface::removeInhibitorService);

    connect(m_manager, &night_color_manager::inhibited_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::enabled_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::runningChanged, this, [this] {
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

    connect(m_manager, &night_color_manager::current_temperature_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::target_temperature_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::mode_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::previous_transition_timings_changed, this, [this] {
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

    connect(m_manager, &night_color_manager::scheduled_transition_timings_changed, this, [this] {
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

bool color_correct_dbus_interface::isInhibited() const
{
    return m_manager->is_inhibited();
}

bool color_correct_dbus_interface::isEnabled() const
{
    return m_manager->is_enabled();
}

bool color_correct_dbus_interface::isRunning() const
{
    return m_manager->is_running();
}

bool color_correct_dbus_interface::isAvailable() const
{
    return m_manager->is_available();
}

int color_correct_dbus_interface::currentTemperature() const
{
    return m_manager->current_temperature();
}

int color_correct_dbus_interface::targetTemperature() const
{
    return m_manager->get_target_temperature();
}

int color_correct_dbus_interface::mode() const
{
    return m_manager->mode();
}

quint64 color_correct_dbus_interface::previousTransitionDateTime() const
{
    auto const dateTime = m_manager->previous_transition_date_time();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 color_correct_dbus_interface::previousTransitionDuration() const
{
    return quint32(m_manager->previous_transition_duration());
}

quint64 color_correct_dbus_interface::scheduledTransitionDateTime() const
{
    const QDateTime dateTime = m_manager->scheduled_transition_date_time();
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 color_correct_dbus_interface::scheduledTransitionDuration() const
{
    return quint32(m_manager->scheduled_transition_duration());
}

void color_correct_dbus_interface::nightColorAutoLocationUpdate(double latitude, double longitude)
{
    m_manager->auto_location_update(latitude, longitude);
}

uint color_correct_dbus_interface::inhibit()
{
    const QString serviceName = QDBusContext::message().service();

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->addWatchedService(serviceName);
    }

    m_inhibitors.insert(serviceName, ++m_lastInhibitionCookie);

    m_manager->inhibit();

    return m_lastInhibitionCookie;
}

void color_correct_dbus_interface::uninhibit(uint cookie)
{
    const QString serviceName = QDBusContext::message().service();

    uninhibit(serviceName, cookie);
}

void color_correct_dbus_interface::uninhibit(const QString& serviceName, uint cookie)
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

void color_correct_dbus_interface::removeInhibitorService(const QString& serviceName)
{
    const auto cookies = m_inhibitors.values(serviceName);
    for (const uint& cookie : cookies) {
        uninhibit(serviceName, cookie);
    }
}

}
