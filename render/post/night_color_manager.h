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

#include "constants.h"

#include "kwin_export.h"
#include "render/types.h"

#include <KConfigWatcher>
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDateTime>
#include <QObject>
#include <QPair>
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

typedef QPair<QDateTime, QDateTime> DateTimes;
typedef QPair<QTime, QTime> Times;

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
class KWIN_EXPORT night_color_manager : public QObject
{
public:
    night_color_manager();
    ~night_color_manager();

    void init();

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
     * Returns @c true if the night color manager is blocked; otherwise @c false.
     */
    bool is_inhibited() const;

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

    /**
     * Returns @c true if Night Color is enabled; otherwise @c false.
     */
    bool is_enabled() const;

    /**
     * Returns @c true if Night Color is currently running; otherwise @c false.
     */
    bool is_running() const;

    /**
     * Returns @c true if Night Color is supported by platform; otherwise @c false.
     */
    bool is_available() const;

    /**
     * Returns the current screen color temperature.
     */
    int current_temperature() const;

    /**
     * Returns the target screen color temperature.
     */
    int get_target_temperature() const;

    /**
     * Returns the mode in which Night Color is operating.
     */
    night_color_mode mode() const;

    /**
     * Returns the datetime that specifies when the previous screen color temperature transition
     * had started. Notice that when Night Color operates in the Constant mode, the returned date
     * time object is not valid.
     */
    QDateTime previous_transition_date_time() const;

    /**
     * Returns the duration of the previous screen color temperature transition, in milliseconds.
     */
    qint64 previous_transition_duration() const;

    /**
     * Returns the datetime that specifies when the next screen color temperature transition will
     * start. Notice that when Night Color operates in the Constant mode, the returned date time
     * object is not valid.
     */
    QDateTime scheduled_transition_date_time() const;

    /**
     * Returns the duration of the next screen color temperature transition, in milliseconds.
     */
    qint64 scheduled_transition_duration() const;

    // for auto tests
    void reconfigure();

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

    std::unique_ptr<color_correct_dbus_interface> dbus;
    std::unique_ptr<base::os::clock::skew_notifier> clock_skew_notifier;

    // Specifies whether Night Color is enabled.
    bool enabled = false;

    // Specifies whether Night Color is currently running.
    bool m_running = false;

    // Specifies whether Night Color is inhibited globally.
    bool is_globally_inhibited = false;

    night_color_mode m_mode{night_color_mode::automatic};

    // the previous and next sunrise/sunset intervals - in UTC time
    DateTimes prev_transition = DateTimes();
    DateTimes next_transition = DateTimes();

    // whether it is currently day or night
    bool daylight{true};

    // manual times from config
    QTime morning_time{QTime(6, 0)};
    QTime evening_time{QTime(18, 0)};

    // saved in minutes > 1
    int transition_time{30};

    // auto location provided by work space
    double lat_auto;
    double lng_auto;

    // manual location from config
    double lat_fixed;
    double lng_fixed;

    QTimer* slow_update_start_timer{nullptr};
    QTimer* slow_update_timer{nullptr};
    QTimer* quick_adjust_timer{nullptr};

    int current_temp{DEFAULT_DAY_TEMPERATURE};
    int target_temp{DEFAULT_DAY_TEMPERATURE};
    int day_target_temp{DEFAULT_DAY_TEMPERATURE};
    int night_target_temp{DEFAULT_NIGHT_TEMPERATURE};

    int failed_commit_attempts{0};
    int inhibit_reference_count{0};

    KConfigWatcher::Ptr config_watcher;
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
