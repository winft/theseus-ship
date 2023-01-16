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
#pragma once

#include "render/types.h"

#include <QObject>
#include <QtDBus>

namespace KWin::render::post
{

struct night_color_data;
class night_color_manager;

class color_correct_dbus_interface : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.ColorCorrect")
    Q_PROPERTY(bool inhibited READ isInhibited)
    Q_PROPERTY(bool enabled READ isEnabled)
    Q_PROPERTY(bool running READ isRunning)
    Q_PROPERTY(bool available READ isAvailable)
    Q_PROPERTY(int currentTemperature READ currentTemperature)
    Q_PROPERTY(int targetTemperature READ targetTemperature)
    Q_PROPERTY(int mode READ mode)
    Q_PROPERTY(quint64 previousTransitionDateTime READ previousTransitionDateTime)
    Q_PROPERTY(quint32 previousTransitionDuration READ previousTransitionDuration)
    Q_PROPERTY(quint64 scheduledTransitionDateTime READ scheduledTransitionDateTime)
    Q_PROPERTY(quint32 scheduledTransitionDuration READ scheduledTransitionDuration)

public:
    explicit color_correct_dbus_interface(night_color_manager* manager,
                                          night_color_data const& data);
    ~color_correct_dbus_interface() override = default;

    bool isInhibited() const;
    bool isEnabled() const;
    bool isRunning() const;
    bool isAvailable() const;
    int currentTemperature() const;
    int targetTemperature() const;
    int mode() const;
    quint64 previousTransitionDateTime() const;
    quint32 previousTransitionDuration() const;
    quint64 scheduledTransitionDateTime() const;
    quint32 scheduledTransitionDuration() const;

    void send_inhibited(bool inhibited) const;
    void send_enabled(bool enabled) const;
    void send_running(bool running) const;
    void send_current_temperature(int temp) const;
    void send_target_temperature(int temp) const;
    void send_mode(night_color_mode mode) const;
    void send_transition_timings() const;

public Q_SLOTS:
    /**
     * @brief For receiving auto location updates, primarily through the KDE Daemon
     * @return void
     * @since 5.12
     */
    void nightColorAutoLocationUpdate(double latitude, double longitude);

    /**
     * @brief Temporarily blocks Night Color.
     * @since 5.18
     */
    uint inhibit();
    /**
     * @brief Cancels the previous call to inhibit().
     * @since 5.18
     */
    void uninhibit(uint cookie);

private Q_SLOTS:
    void removeInhibitorService(const QString& serviceName);

private:
    void uninhibit(const QString& serviceName, uint cookie);

    night_color_manager* m_manager;
    night_color_data const& data;
    QDBusServiceWatcher* m_inhibitorWatcher;
    QMultiHash<QString, uint> m_inhibitors;
    uint m_lastInhibitionCookie = 0;
};

}
