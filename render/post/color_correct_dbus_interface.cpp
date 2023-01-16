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
    QVariantMap pprops;
    pprops.insert(QStringLiteral("previousTransitionDateTime"), previousTransitionDateTime());
    pprops.insert(QStringLiteral("previousTransitionDuration"), previousTransitionDuration());

    QVariantMap sprops;
    sprops.insert(QStringLiteral("scheduledTransitionDateTime"), scheduledTransitionDateTime());
    sprops.insert(QStringLiteral("scheduledTransitionDuration"), scheduledTransitionDuration());

    send_changed_properties(pprops);
    send_changed_properties(sprops);
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
