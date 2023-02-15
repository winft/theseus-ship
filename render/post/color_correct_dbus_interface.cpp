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
#include "night_color_data.h"

#include <QDBusMessage>

namespace KWin::render::post
{

static void send_changed_properties(QVariantMap const& props)
{
    auto message = QDBusMessage::createSignal(QStringLiteral("/ColorCorrect"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"),
                                              QStringLiteral("PropertiesChanged"));

    message.setArguments({
        QStringLiteral("org.kde.kwin.ColorCorrect"),
        props,
        QStringList(), // invalidated_properties
    });

    QDBusConnection::sessionBus().send(message);
}

color_correct_dbus_interface::color_correct_dbus_interface(
    color_correct_dbus_integration integration)
    : integration{std::move(integration)}
    , m_inhibitorWatcher(new QDBusServiceWatcher(this))
{
    m_inhibitorWatcher->setConnection(QDBusConnection::sessionBus());
    m_inhibitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_inhibitorWatcher,
            &QDBusServiceWatcher::serviceUnregistered,
            this,
            &color_correct_dbus_interface::removeInhibitorService);

    new ColorCorrectAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/ColorCorrect"), this);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.NightColor"));
}

color_correct_dbus_interface::~color_correct_dbus_interface()
{
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.NightColor"));
}

bool color_correct_dbus_interface::isInhibited() const
{
    return integration.data.inhibit_reference_count;
}

bool color_correct_dbus_interface::isEnabled() const
{
    return integration.data.enabled;
}

bool color_correct_dbus_interface::isRunning() const
{
    return integration.data.running;
}

bool color_correct_dbus_interface::isAvailable() const
{
    return integration.data.available;
}

int color_correct_dbus_interface::currentTemperature() const
{
    return integration.data.temperature.current;
}

int color_correct_dbus_interface::targetTemperature() const
{
    return integration.data.temperature.target;
}

int color_correct_dbus_interface::mode() const
{
    return integration.data.mode;
}

quint64 color_correct_dbus_interface::previousTransitionDateTime() const
{
    auto const dateTime = integration.data.transition.prev.first;
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 color_correct_dbus_interface::previousTransitionDuration() const
{
    return quint32(integration.data.previous_transition_duration());
}

quint64 color_correct_dbus_interface::scheduledTransitionDateTime() const
{
    auto const dateTime = integration.data.transition.next.first;
    if (dateTime.isValid()) {
        return quint64(dateTime.toSecsSinceEpoch());
    }
    return 0;
}

quint32 color_correct_dbus_interface::scheduledTransitionDuration() const
{
    return quint32(integration.data.scheduled_transition_duration());
}

void color_correct_dbus_interface::send_inhibited(bool inhibited) const
{
    QVariantMap props;
    props.insert(QStringLiteral("inhibited"), inhibited);
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_enabled(bool enabled) const
{
    QVariantMap props;
    props.insert(QStringLiteral("enabled"), enabled);
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_running(bool running) const
{
    QVariantMap props;
    props.insert(QStringLiteral("running"), running);
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_current_temperature(int temp) const
{
    QVariantMap props;
    props.insert(QStringLiteral("currentTemperature"), temp);
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_target_temperature(int temp) const
{
    QVariantMap props;
    props.insert(QStringLiteral("targetTemperature"), temp);
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_mode(night_color_mode mode) const
{
    QVariantMap props;
    props.insert(QStringLiteral("mode"), static_cast<uint>(mode));
    send_changed_properties(props);
}

void color_correct_dbus_interface::send_transition_timings() const
{
    QVariantMap props;
    props.insert(QStringLiteral("previousTransitionDateTime"), previousTransitionDateTime());
    props.insert(QStringLiteral("previousTransitionDuration"), previousTransitionDuration());

    props.insert(QStringLiteral("scheduledTransitionDateTime"), scheduledTransitionDateTime());
    props.insert(QStringLiteral("scheduledTransitionDuration"), scheduledTransitionDuration());

    send_changed_properties(props);
}

void color_correct_dbus_interface::nightColorAutoLocationUpdate(double latitude, double longitude)
{
    integration.loc_update(latitude, longitude);
}

uint color_correct_dbus_interface::inhibit()
{
    const QString serviceName = QDBusContext::message().service();

    if (!m_inhibitors.contains(serviceName)) {
        m_inhibitorWatcher->addWatchedService(serviceName);
    }

    m_inhibitors.insert(serviceName, ++m_lastInhibitionCookie);

    integration.inhibit(true);

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

    integration.inhibit(false);
}

void color_correct_dbus_interface::removeInhibitorService(const QString& serviceName)
{
    const auto cookies = m_inhibitors.values(serviceName);
    for (const uint& cookie : cookies) {
        uninhibit(serviceName, cookie);
    }
}

}
