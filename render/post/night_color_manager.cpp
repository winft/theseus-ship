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
#include "night_color_manager.h"

#include "base/os/clock/skew_notifier.h"
#include "color_correct_dbus_interface.h"
#include "suncalc.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "base/seat/session.h"
#include "input/platform.h"
#include "main.h"
#include "utils/gamma_ramp.h"
#include "win/space.h"

#include "color_correct_settings.h"

#include <QTimer>

namespace KWin::render::post
{

static const int QUICK_ADJUST_DURATION = 2000;
static const int TEMPERATURE_STEP = 50;

static bool checkLocation(double lat, double lng)
{
    return -90 <= lat && lat <= 90 && -180 <= lng && lng <= 180;
}

night_color_manager::night_color_manager()
    : qobject{std::make_unique<QObject>()}
    , dbus{std::make_unique<color_correct_dbus_interface>(this, data)}
    , clock_skew_notifier{std::make_unique<base::os::clock::skew_notifier>()}
{
    Settings::instance(kwinApp()->config());

    config_watcher = KConfigWatcher::create(kwinApp()->config());
    QObject::connect(config_watcher.data(), &KConfigWatcher::configChanged, qobject.get(), [this] {
        reconfigure();
    });

    // we may always read in the current config
    read_config();

    if (!data.available) {
        return;
    }

    QObject::connect(&kwinApp()->get_base(), &base::platform::output_added, qobject.get(), [this] {
        hard_reset();
    });
    QObject::connect(&kwinApp()->get_base(),
                     &base::platform::output_removed,
                     qobject.get(),
                     [this] { hard_reset(); });

    QObject::connect(kwinApp()->session.get(),
                     &base::seat::session::sessionActiveChanged,
                     qobject.get(),
                     [this](bool active) {
                         if (active) {
                             hard_reset();
                         } else {
                             cancel_all_timers();
                         }
                     });

    QObject::connect(clock_skew_notifier.get(),
                     &base::os::clock::skew_notifier::skewed,
                     qobject.get(),
                     [this]() {
                         // check if we're resuming from suspend - in this case do a hard reset
                         // Note: We're using the time clock to detect a suspend phase instead of
                         // connecting to the
                         //       provided logind dbus signal, because this signal would be received
                         //       way too late.
                         QDBusMessage message
                             = QDBusMessage::createMethodCall("org.freedesktop.login1",
                                                              "/org/freedesktop/login1",
                                                              "org.freedesktop.DBus.Properties",
                                                              QStringLiteral("Get"));
                         message.setArguments(QVariantList({"org.freedesktop.login1.Manager",
                                                            QStringLiteral("PreparingForSleep")}));
                         QDBusReply<QVariant> reply = QDBusConnection::systemBus().call(message);
                         bool comingFromSuspend;
                         if (reply.isValid()) {
                             comingFromSuspend = reply.value().toBool();
                         } else {
                             qCDebug(KWIN_CORE)
                                 << "Failed to get PreparingForSleep Property of logind session:"
                                 << reply.error().message();
                             // Always do a hard reset in case we have no further information.
                             comingFromSuspend = true;
                         }

                         if (comingFromSuspend) {
                             hard_reset();
                         } else {
                             reset_all_timers();
                         }
                     });

    hard_reset();
}

night_color_manager::~night_color_manager() = default;

void night_color_manager::hard_reset()
{
    cancel_all_timers();

    update_transition_timings(true);
    update_target_temperature();

    if (data.available && data.enabled && !data.inhibited()) {
        set_running(true);
        commit_gamma_ramps(current_target_temp());
    }
    reset_all_timers();
}

void night_color_manager::reconfigure()
{
    cancel_all_timers();
    read_config();
    reset_all_timers();
}

void night_color_manager::toggle()
{
    data.globally_inhibited = !data.globally_inhibited;
    data.globally_inhibited ? inhibit() : uninhibit();
}

void night_color_manager::inhibit()
{
    data.inhibit_reference_count++;

    if (data.inhibit_reference_count == 1) {
        reset_all_timers();
        night_color_display_inhibit_message(true);
        dbus->send_inhibited(true);
    }
}

void night_color_manager::uninhibit()
{
    data.inhibit_reference_count--;

    if (!data.inhibit_reference_count) {
        reset_all_timers();
        night_color_display_inhibit_message(false);
        dbus->send_inhibited(false);
    }
}

void night_color_manager::read_config()
{
    auto settings = Settings::self();
    settings->load();

    set_enabled(settings->active());

    auto const mode = settings->mode();
    switch (settings->mode()) {
    case night_color_mode::automatic:
    case night_color_mode::location:
    case night_color_mode::timings:
    case night_color_mode::constant:
        set_mode(mode);
        break;
    default:
        // Fallback for invalid setting values.
        set_mode(night_color_mode::automatic);
        break;
    }

    data.temperature.day_target
        = qBound(MIN_TEMPERATURE, settings->dayTemperature(), DEFAULT_DAY_TEMPERATURE);
    data.temperature.night_target
        = qBound(MIN_TEMPERATURE, settings->nightTemperature(), DEFAULT_DAY_TEMPERATURE);

    double lat, lng;
    auto correctReadin = [&lat, &lng]() {
        if (!checkLocation(lat, lng)) {
            // out of domain
            lat = 0;
            lng = 0;
        }
    };
    // automatic
    lat = settings->latitudeAuto();
    lng = settings->longitudeAuto();
    correctReadin();
    data.auto_loc.lat = lat;
    data.auto_loc.lng = lng;
    // fixed location
    lat = settings->latitudeFixed();
    lng = settings->longitudeFixed();
    correctReadin();
    data.man_loc.lat = lat;
    data.man_loc.lng = lng;

    // fixed timings
    auto mrB = QTime::fromString(settings->morningBeginFixed(), "hhmm");
    auto evB = QTime::fromString(settings->eveningBeginFixed(), "hhmm");

    int diffME = evB > mrB ? mrB.msecsTo(evB) : evB.msecsTo(mrB);
    int diffMin = qMin(diffME, MSC_DAY - diffME);

    int trTime = settings->transitionTime() * 1000 * 60;
    if (trTime < 0 || diffMin <= trTime) {
        // transition time too long - use defaults
        mrB = QTime(6, 0);
        evB = QTime(18, 0);
        trTime = FALLBACK_SLOW_UPDATE_TIME;
    }
    data.man_time.morning = mrB;
    data.man_time.evening = evB;
    data.transition.duration = qMax(trTime / 1000 / 60, 1);
}

void night_color_manager::reset_all_timers()
{
    cancel_all_timers();
    if (data.available) {
        set_running(data.enabled && !data.inhibited());
        // we do this also for active being false in order to reset the temperature back to the day
        // value
        reset_quick_adjust_timer();
    } else {
        set_running(false);
    }
}

void night_color_manager::cancel_all_timers()
{
    delete slow_update_start_timer;
    delete slow_update_timer;
    delete quick_adjust_timer;

    slow_update_start_timer = nullptr;
    slow_update_timer = nullptr;
    quick_adjust_timer = nullptr;
}

void night_color_manager::reset_quick_adjust_timer()
{
    update_transition_timings(false);
    update_target_temperature();

    int tempDiff = qAbs(current_target_temp() - data.temperature.current);
    // allow tolerance of one TEMPERATURE_STEP to compensate if a slow update is coincidental
    if (tempDiff > TEMPERATURE_STEP) {
        cancel_all_timers();
        quick_adjust_timer = new QTimer(qobject.get());
        quick_adjust_timer->setSingleShot(false);
        QObject::connect(
            quick_adjust_timer, &QTimer::timeout, qobject.get(), [this] { quick_adjust(); });

        int interval = QUICK_ADJUST_DURATION / (tempDiff / TEMPERATURE_STEP);
        if (interval == 0) {
            interval = 1;
        }
        quick_adjust_timer->start(interval);
    } else {
        reset_slow_update_start_timer();
    }
}

void night_color_manager::quick_adjust()
{
    if (!quick_adjust_timer) {
        return;
    }

    int nextTemp;
    auto const targetTemp = current_target_temp();

    if (data.temperature.current < targetTemp) {
        nextTemp = qMin(data.temperature.current + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(data.temperature.current - TEMPERATURE_STEP, targetTemp);
    }
    commit_gamma_ramps(nextTemp);

    if (nextTemp == targetTemp) {
        // stop timer, we reached the target temp
        delete quick_adjust_timer;
        quick_adjust_timer = nullptr;
        reset_slow_update_start_timer();
    }
}

void night_color_manager::reset_slow_update_start_timer()
{
    delete slow_update_start_timer;
    slow_update_start_timer = nullptr;

    if (!data.running || quick_adjust_timer) {
        // only reenable the slow update start timer when quick adjust is not active anymore
        return;
    }

    // There is no need for starting the slow update timer. Screen color temperature
    // will be constant all the time now.
    if (data.mode == night_color_mode::constant) {
        return;
    }

    // set up the next slow update
    slow_update_start_timer = new QTimer(qobject.get());
    slow_update_start_timer->setSingleShot(true);
    QObject::connect(slow_update_start_timer, &QTimer::timeout, qobject.get(), [this] {
        reset_slow_update_start_timer();
    });

    update_transition_timings(false);
    update_target_temperature();

    const int diff = QDateTime::currentDateTime().msecsTo(data.transition.next.first);
    if (diff <= 0) {
        qCCritical(KWIN_CORE) << "Error in time calculation. Deactivating Night Color.";
        return;
    }
    slow_update_start_timer->start(diff);

    // start the current slow update
    reset_slow_update_timer();
}

void night_color_manager::reset_slow_update_timer()
{
    delete slow_update_timer;
    slow_update_timer = nullptr;

    const QDateTime now = QDateTime::currentDateTime();
    const bool isDay = daylight;
    const int targetTemp = isDay ? data.temperature.day_target : data.temperature.night_target;

    // We've reached the target color temperature or the transition time is zero.
    if (data.transition.prev.first == data.transition.prev.second
        || data.temperature.current == targetTemp) {
        commit_gamma_ramps(targetTemp);
        return;
    }

    if (data.transition.prev.first <= now && now <= data.transition.prev.second) {
        int availTime = now.msecsTo(data.transition.prev.second);
        slow_update_timer = new QTimer(qobject.get());
        slow_update_timer->setSingleShot(false);
        if (isDay) {
            QObject::connect(slow_update_timer, &QTimer::timeout, qobject.get(), [this] {
                slow_update(data.temperature.day_target);
            });
        } else {
            QObject::connect(slow_update_timer, &QTimer::timeout, qobject.get(), [this] {
                slow_update(data.temperature.night_target);
            });
        }

        // calculate interval such as temperature is changed by TEMPERATURE_STEP K per timer timeout
        int interval = availTime * TEMPERATURE_STEP / qAbs(targetTemp - data.temperature.current);
        if (interval == 0) {
            interval = 1;
        }
        slow_update_timer->start(interval);
    }
}

void night_color_manager::slow_update(int targetTemp)
{
    if (!slow_update_timer) {
        return;
    }
    int nextTemp;
    if (data.temperature.current < targetTemp) {
        nextTemp = qMin(data.temperature.current + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(data.temperature.current - TEMPERATURE_STEP, targetTemp);
    }
    commit_gamma_ramps(nextTemp);
    if (nextTemp == targetTemp) {
        // stop timer, we reached the target temp
        delete slow_update_timer;
        slow_update_timer = nullptr;
    }
}

void night_color_manager::update_target_temperature()
{
    const int targetTemperature = data.mode != night_color_mode::constant && data.daylight
        ? data.temperature.day_target
        : data.temperature.night_target;

    if (data.temperature.target == targetTemperature) {
        return;
    }

    data.temperature.target = targetTemperature;
    dbus->send_target_temperature(targetTemperature);
}

void night_color_manager::update_transition_timings(bool force)
{
    if (data.mode == night_color_mode::constant) {
        data.transition.next = DateTimes();
        data.transition.prev = DateTimes();
        dbus->send_transition_timings();
        return;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    if (data.mode == night_color_mode::timings) {
        const QDateTime nextMorB
            = QDateTime(todayNow.date().addDays(data.man_time.morning < todayNow.time()),
                        data.man_time.morning);
        const QDateTime nextMorE = nextMorB.addSecs(data.transition.duration * 60);
        const QDateTime nextEveB
            = QDateTime(todayNow.date().addDays(data.man_time.evening < todayNow.time()),
                        data.man_time.evening);
        const QDateTime nextEveE = nextEveB.addSecs(data.transition.duration * 60);

        if (nextEveB < nextMorB) {
            data.daylight = true;
            data.transition.next = DateTimes(nextEveB, nextEveE);
            data.transition.prev = DateTimes(nextMorB.addDays(-1), nextMorE.addDays(-1));
        } else {
            data.daylight = false;
            data.transition.next = DateTimes(nextMorB, nextMorE);
            data.transition.prev = DateTimes(nextEveB.addDays(-1), nextEveE.addDays(-1));
        }
        dbus->send_transition_timings();
        return;
    }

    double lat, lng;
    if (data.mode == night_color_mode::automatic) {
        lat = data.auto_loc.lat;
        lng = data.auto_loc.lng;
    } else {
        lat = data.man_loc.lat;
        lng = data.man_loc.lng;
    }

    if (!force) {
        // first try by only switching the timings
        if (data.transition.prev.first.date() == data.transition.next.first.date()) {
            // next is evening
            data.daylight = true;
            data.transition.prev = data.transition.next;
            data.transition.next = get_sun_timings(todayNow, lat, lng, false);
        } else {
            // next is morning
            data.daylight = false;
            data.transition.prev = data.transition.next;
            data.transition.next = get_sun_timings(todayNow.addDays(1), lat, lng, true);
        }
    }

    if (force || !check_automatic_sun_timings()) {
        // in case this fails, reset them
        DateTimes morning = get_sun_timings(todayNow, lat, lng, true);
        if (todayNow < morning.first) {
            data.daylight = false;
            data.transition.prev = get_sun_timings(todayNow.addDays(-1), lat, lng, false);
            data.transition.next = morning;
        } else {
            DateTimes evening = get_sun_timings(todayNow, lat, lng, false);
            if (todayNow < evening.first) {
                data.daylight = true;
                data.transition.prev = morning;
                data.transition.next = evening;
            } else {
                data.daylight = false;
                data.transition.prev = evening;
                data.transition.next = get_sun_timings(todayNow.addDays(1), lat, lng, true);
            }
        }
    }

    dbus->send_transition_timings();
}

DateTimes night_color_manager::get_sun_timings(const QDateTime& dateTime,
                                               double latitude,
                                               double longitude,
                                               bool at_morning) const
{
    auto dateTimes = calculate_sun_timings(dateTime, latitude, longitude, at_morning);
    // At locations near the poles it is possible, that we can't
    // calculate some or all sun timings (midnight sun).
    // In this case try to fallback to sensible default values.
    const bool beginDefined = !dateTimes.first.isNull();
    const bool endDefined = !dateTimes.second.isNull();
    if (!beginDefined || !endDefined) {
        if (beginDefined) {
            dateTimes.second = dateTimes.first.addMSecs(FALLBACK_SLOW_UPDATE_TIME);
        } else if (endDefined) {
            dateTimes.first = dateTimes.second.addMSecs(-FALLBACK_SLOW_UPDATE_TIME);
        } else {
            // Just use default values for morning and evening, but the user
            // will probably deactivate Night Color anyway if he is living
            // in a region without clear sun rise and set.
            const QTime referenceTime = at_morning ? QTime(6, 0) : QTime(18, 0);
            dateTimes.first = QDateTime(dateTime.date(), referenceTime);
            dateTimes.second = dateTimes.first.addMSecs(FALLBACK_SLOW_UPDATE_TIME);
        }
    }
    return dateTimes;
}

bool night_color_manager::check_automatic_sun_timings() const
{
    if (data.transition.prev.first.isValid() && data.transition.prev.second.isValid()
        && data.transition.next.first.isValid() && data.transition.next.second.isValid()) {
        const QDateTime todayNow = QDateTime::currentDateTime();
        return data.transition.prev.first <= todayNow && todayNow < data.transition.next.first
            && data.transition.prev.first.msecsTo(data.transition.next.first) < MSC_DAY * 23. / 24;
    }
    return false;
}

int night_color_manager::current_target_temp() const
{
    if (!data.running) {
        return DEFAULT_DAY_TEMPERATURE;
    }

    if (data.mode == night_color_mode::constant) {
        return data.temperature.night_target;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    auto f = [this, todayNow](int target1, int target2) {
        if (todayNow <= data.transition.prev.second) {
            double residueQuota = todayNow.msecsTo(data.transition.prev.second)
                / static_cast<double>(data.transition.prev.first.msecsTo(
                    data.transition.prev.second));

            double ret = static_cast<int>(((1. - residueQuota) * static_cast<double>(target2)
                                           + residueQuota * static_cast<double>(target1)));
            // remove single digits
            ret = (static_cast<int>((0.1 * ret))) * 10;
            return static_cast<int>(ret);
        } else {
            return target2;
        }
    };

    if (data.daylight) {
        return f(data.temperature.night_target, data.temperature.day_target);
    } else {
        return f(data.temperature.day_target, data.temperature.night_target);
    }
}

void night_color_manager::commit_gamma_ramps(int temperature)
{
    for (auto output : kwinApp()->get_base().get_outputs()) {
        auto rampsize = output->gamma_ramp_size();
        if (!rampsize) {
            continue;
        }

        gamma_ramp ramp(rampsize);

        /*
         * The gamma calculation below is based on the Redshift app:
         * https://github.com/jonls/redshift
         */
        uint16_t* red = ramp.red();
        uint16_t* green = ramp.green();
        uint16_t* blue = ramp.blue();

        // linear default state
        for (int i = 0; i < rampsize; i++) {
            uint16_t value = static_cast<double>(i) / rampsize * (UINT16_MAX + 1);
            red[i] = value;
            green[i] = value;
            blue[i] = value;
        }

        // approximate white point
        float whitePoint[3];
        float alpha = (temperature % 100) / 100.;
        int bbCIndex = ((temperature - 1000) / 100) * 3;
        whitePoint[0]
            = (1. - alpha) * blackbody_color[bbCIndex] + alpha * blackbody_color[bbCIndex + 3];
        whitePoint[1]
            = (1. - alpha) * blackbody_color[bbCIndex + 1] + alpha * blackbody_color[bbCIndex + 4];
        whitePoint[2]
            = (1. - alpha) * blackbody_color[bbCIndex + 2] + alpha * blackbody_color[bbCIndex + 5];

        for (int i = 0; i < rampsize; i++) {
            red[i] = qreal(red[i]) / (UINT16_MAX + 1) * whitePoint[0] * (UINT16_MAX + 1);
            green[i] = qreal(green[i]) / (UINT16_MAX + 1) * whitePoint[1] * (UINT16_MAX + 1);
            blue[i] = qreal(blue[i]) / (UINT16_MAX + 1) * whitePoint[2] * (UINT16_MAX + 1);
        }

        if (output->set_gamma_ramp(ramp)) {
            set_current_temperature(temperature);
            data.failed_commit_attempts = 0;
        } else {
            data.failed_commit_attempts++;
            if (data.failed_commit_attempts < 10) {
                qCWarning(KWIN_CORE).nospace()
                    << "Committing Gamma Ramp failed for output " << output->name() << ". Trying "
                    << (10 - data.failed_commit_attempts) << " times more.";
            } else {
                // TODO: On multi monitor setups we could try to rollback earlier changes for
                // already committed outputs
                qCWarning(KWIN_CORE)
                    << "Gamma Ramp commit failed too often. Deactivating color correction for now.";
                // reset so we can try again later (i.e. after suspend phase or config change)
                data.failed_commit_attempts = 0;
                set_running(false);
                cancel_all_timers();
            }
        }
    }
}

void night_color_manager::auto_location_update(double latitude, double longitude)
{
    qCDebug(KWIN_CORE, "Received new location (lat: %f, lng: %f)", latitude, longitude);

    if (!checkLocation(latitude, longitude)) {
        return;
    }

    // we tolerate small deviations with minimal impact on sun timings
    if (qAbs(data.auto_loc.lat - latitude) < 2 && qAbs(data.auto_loc.lng - longitude) < 1) {
        return;
    }
    cancel_all_timers();
    data.auto_loc.lat = latitude;
    data.auto_loc.lng = longitude;

    auto settings = Settings::self();
    settings->setLatitudeAuto(latitude);
    settings->setLongitudeAuto(longitude);
    settings->save();

    reset_all_timers();
}

void night_color_manager::set_enabled(bool enable)
{
    if (data.enabled == enable) {
        return;
    }
    data.enabled = enable;
    clock_skew_notifier->set_active(data.enabled);
    dbus->send_enabled(enable);
}

void night_color_manager::set_running(bool running)
{
    if (data.running == running) {
        return;
    }
    data.running = running;
    dbus->send_running(running);
}

void night_color_manager::set_current_temperature(int temperature)
{
    if (data.temperature.current == temperature) {
        return;
    }
    data.temperature.current = temperature;
    dbus->send_current_temperature(temperature);
}

void night_color_manager::set_mode(night_color_mode mode)
{
    if (data.mode == mode) {
        return;
    }
    data.mode = mode;
    dbus->send_mode(mode);
}

}
