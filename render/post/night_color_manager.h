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

#include "night_color_data.h"

#include "kwin_export.h"

#include <KConfigWatcher>
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QObject>
#include <memory>

class QTimer;

namespace KWin
{

namespace base::os::clock
{
class skew_notifier;
}

namespace render::post
{

class color_correct_dbus_interface;

/**
 * The night color manager is a blue light filter similar to Redshift.
 *
 * There are four modes this manager can operate in: Automatic, Location, Timings,
 * and Constant. Both Automatic and Location modes derive screen color temperature
 * from the current position of the Sun, the only difference between two is how
 * coordinates of the user are specified. If the user is located near the North or
 * South pole, we can't compute correct position of the Sun, that's why we need
 * Timings and Constant mode.
 *
 * With the Timings mode, screen color temperature is computed based on the clock
 * time. The user needs to specify timings of the sunset and sunrise as well the
 * transition time.
 *
 * With the Constant mode, screen color temperature is always constant.
 */
class KWIN_EXPORT night_color_manager
{
public:
    night_color_manager();
    ~night_color_manager();

    void auto_location_update(double latitude, double longitude);

    /**
     * Toggles the active state of the filter.
     *
     * A quick transition will be started if the difference between current screen
     * color temperature and target screen color temperature is too large. Target
     * temperature is defined in context of the new active state.
     *
     * If the filter becomes inactive after calling this method, the target color
     * temperature is 6500 K.
     *
     * If the filter becomes active after calling this method, the target screen
     * color temperature is defined by the current operation mode.
     *
     * Note that this method is a no-op if the underlying platform doesn't support
     * adjusting gamma ramps.
     */
    void toggle();

    /**
     * Temporarily blocks the night color manager.
     *
     * After calling this method, the screen color temperature will be reverted
     * back to 6500C. When you're done, call uninhibit() method.
     */
    void inhibit();

    /**
     * Attempts to unblock the night color manager.
     */
    void uninhibit();

    // for auto tests
    void reconfigure();

    std::unique_ptr<QObject> qobject;
    night_color_data data;

private:
    void read_config();
    void hard_reset();

    void slow_update(int targetTemp);
    void quick_adjust();

    void reset_slow_update_start_timer();
    void reset_all_timers();
    void cancel_all_timers();

    /**
     * Quick shift on manual change to current target Temperature
     */
    void reset_quick_adjust_timer();
    /**
     * Slow shift to daytime target Temperature
     */
    void reset_slow_update_timer();

    int current_target_temp() const;
    void update_target_temperature();
    void update_transition_timings(bool force);
    DateTimes get_sun_timings(const QDateTime& dateTime,
                              double latitude,
                              double longitude,
                              bool at_morning) const;
    bool check_automatic_sun_timings() const;

    void commit_gamma_ramps(int temperature);

    void set_enabled(bool enable);
    void set_running(bool running);
    void set_current_temperature(int temperature);
    void set_mode(night_color_mode mode);

    KConfigWatcher::Ptr config_watcher;

    std::unique_ptr<color_correct_dbus_interface> dbus;
    std::unique_ptr<base::os::clock::skew_notifier> clock_skew_notifier;

    QTimer* slow_update_start_timer{nullptr};
    QTimer* slow_update_timer{nullptr};
    QTimer* quick_adjust_timer{nullptr};
};

inline void night_color_display_inhibit_message(bool inhibit)
{
    // TODO: Maybe use different icons?
    auto const icon = inhibit ? QStringLiteral("preferences-desktop-display-nightcolor-off")
                              : QStringLiteral("preferences-desktop-display-nightcolor-on");

    auto const text = inhibit ? i18nc("Night Color was disabled", "Night Color Off")
                              : i18nc("Night Color was enabled", "Night Color On");

    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                  QStringLiteral("/org/kde/osdService"),
                                                  QStringLiteral("org.kde.osdService"),
                                                  QStringLiteral("showText"));
    message.setArguments({icon, text});

    QDBusConnection::sessionBus().asyncCall(message);
}

}
}
