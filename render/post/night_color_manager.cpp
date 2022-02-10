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
#include "base/platform.h"
#include "base/seat/session.h"
#include "input/redirect.h"
#include "utils/gamma_ramp.h"
#include <base/output.h>
#include <main.h>
#include <screens.h>
#include <workspace.h>

#include <color_correct_settings.h>

#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QDBusConnection>
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
    : dbus{std::make_unique<color_correct_dbus_interface>(this)}
    , clock_skew_notifier{std::make_unique<base::os::clock::skew_notifier>()}
{
    connect(kwinApp(), &Application::startup_finished, this, &night_color_manager::init);

    // Display a message when Night Color is (un)inhibited.
    connect(this, &night_color_manager::inhibited_changed, this, [this] {
        // TODO: Maybe use different icons?
        const QString iconName = is_inhibited()
            ? QStringLiteral("preferences-desktop-display-nightcolor-off")
            : QStringLiteral("preferences-desktop-display-nightcolor-on");

        const QString text = is_inhibited() ? i18nc("Night Color was disabled", "Night Color Off")
                                            : i18nc("Night Color was enabled", "Night Color On");

        QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                              QStringLiteral("/org/kde/osdService"),
                                                              QStringLiteral("org.kde.osdService"),
                                                              QStringLiteral("showText"));
        message.setArguments({iconName, text});

        QDBusConnection::sessionBus().asyncCall(message);
    });
}

night_color_manager::~night_color_manager() = default;

void night_color_manager::init()
{
    Settings::instance(kwinApp()->config());

    config_watcher = KConfigWatcher::create(kwinApp()->config());
    QObject::connect(config_watcher.data(),
                     &KConfigWatcher::configChanged,
                     this,
                     &night_color_manager::reconfigure);

    // we may always read in the current config
    read_config();

    if (!is_available()) {
        return;
    }

    connect(&kwinApp()->get_base().screens,
            &Screens::countChanged,
            this,
            &night_color_manager::hard_reset);

    QObject::connect(kwinApp()->session.get(),
                     &base::seat::session::sessionActiveChanged,
                     this,
                     [this](bool active) {
                         if (active) {
                             hard_reset();
                         } else {
                             cancel_all_timers();
                         }
                     });

    connect(clock_skew_notifier.get(), &base::os::clock::skew_notifier::skewed, this, [this]() {
        // check if we're resuming from suspend - in this case do a hard reset
        // Note: We're using the time clock to detect a suspend phase instead of connecting to the
        //       provided logind dbus signal, because this signal would be received way too late.
        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.login1",
                                                              "/org/freedesktop/login1",
                                                              "org.freedesktop.DBus.Properties",
                                                              QStringLiteral("Get"));
        message.setArguments(
            QVariantList({"org.freedesktop.login1.Manager", QStringLiteral("PreparingForSleep")}));
        QDBusReply<QVariant> reply = QDBusConnection::systemBus().call(message);
        bool comingFromSuspend;
        if (reply.isValid()) {
            comingFromSuspend = reply.value().toBool();
        } else {
            qCDebug(KWIN_CORE) << "Failed to get PreparingForSleep Property of logind session:"
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

void night_color_manager::hard_reset()
{
    cancel_all_timers();

    update_transition_timings(true);
    update_target_temperature();

    if (is_available() && is_enabled() && !is_inhibited()) {
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
    is_globally_inhibited = !is_globally_inhibited;
    is_globally_inhibited ? inhibit() : uninhibit();
}

bool night_color_manager::is_inhibited() const
{
    return inhibit_reference_count;
}

void night_color_manager::inhibit()
{
    inhibit_reference_count++;

    if (inhibit_reference_count == 1) {
        reset_all_timers();
        Q_EMIT inhibited_changed();
    }
}

void night_color_manager::uninhibit()
{
    inhibit_reference_count--;

    if (!inhibit_reference_count) {
        reset_all_timers();
        Q_EMIT inhibited_changed();
    }
}

bool night_color_manager::is_enabled() const
{
    return enabled;
}

bool night_color_manager::is_running() const
{
    return m_running;
}

bool night_color_manager::is_available() const
{
    // TODO(romangg): That depended in the past on the hardware backend in use. But right now all
    //                backends support gamma control. We might remove this in the future.
    return true;
}

int night_color_manager::current_temperature() const
{
    return current_temp;
}

int night_color_manager::get_target_temperature() const
{
    return target_temp;
}

night_color_mode night_color_manager::mode() const
{
    return m_mode;
}

QDateTime night_color_manager::previous_transition_date_time() const
{
    return prev_transition.first;
}

qint64 night_color_manager::previous_transition_duration() const
{
    return prev_transition.first.msecsTo(prev_transition.second);
}

QDateTime night_color_manager::scheduled_transition_date_time() const
{
    return next_transition.first;
}

qint64 night_color_manager::scheduled_transition_duration() const
{
    return next_transition.first.msecsTo(next_transition.second);
}

void night_color_manager::init_shortcuts()
{
    // legacy shortcut with localized key (to avoid breaking existing config)
    if (i18n("Toggle Night Color") != QStringLiteral("Toggle Night Color")) {
        QAction toggleActionLegacy;
        toggleActionLegacy.setProperty("componentName", QStringLiteral(KWIN_NAME));
        toggleActionLegacy.setObjectName(i18n("Toggle Night Color"));
        KGlobalAccel::self()->removeAllShortcuts(&toggleActionLegacy);
    }

    QAction* toggleAction = new QAction(this);
    toggleAction->setProperty("componentName", QStringLiteral(KWIN_NAME));
    toggleAction->setObjectName(QStringLiteral("Toggle Night Color"));
    toggleAction->setText(i18n("Toggle Night Color"));
    KGlobalAccel::setGlobalShortcut(toggleAction, QList<QKeySequence>());
    kwinApp()->input->redirect->registerShortcut(
        QKeySequence(), toggleAction, this, &night_color_manager::toggle);
}

void night_color_manager::read_config()
{
    Settings* s = Settings::self();
    s->load();

    set_enabled(s->active());

    auto const mode = s->mode();
    switch (s->mode()) {
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

    night_target_temp = qBound(MIN_TEMPERATURE, s->nightTemperature(), NEUTRAL_TEMPERATURE);

    double lat, lng;
    auto correctReadin = [&lat, &lng]() {
        if (!checkLocation(lat, lng)) {
            // out of domain
            lat = 0;
            lng = 0;
        }
    };
    // automatic
    lat = s->latitudeAuto();
    lng = s->longitudeAuto();
    correctReadin();
    lat_auto = lat;
    lng_auto = lng;
    // fixed location
    lat = s->latitudeFixed();
    lng = s->longitudeFixed();
    correctReadin();
    lat_fixed = lat;
    lng_fixed = lng;

    // fixed timings
    QTime mrB = QTime::fromString(s->morningBeginFixed(), "hhmm");
    QTime evB = QTime::fromString(s->eveningBeginFixed(), "hhmm");

    int diffME = mrB.msecsTo(evB);
    if (diffME <= 0) {
        // morning not strictly before evening - use defaults
        mrB = QTime(6, 0);
        evB = QTime(18, 0);
        diffME = mrB.msecsTo(evB);
    }
    int diffMin = qMin(diffME, MSC_DAY - diffME);

    int trTime = s->transitionTime() * 1000 * 60;
    if (trTime < 0 || diffMin <= trTime) {
        // transition time too long - use defaults
        mrB = QTime(6, 0);
        evB = QTime(18, 0);
        trTime = FALLBACK_SLOW_UPDATE_TIME;
    }
    morning_time = mrB;
    evening_time = evB;
    transition_time = qMax(trTime / 1000 / 60, 1);
}

void night_color_manager::reset_all_timers()
{
    cancel_all_timers();
    if (is_available()) {
        set_running(is_enabled() && !is_inhibited());
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

    int tempDiff = qAbs(current_target_temp() - current_temp);
    // allow tolerance of one TEMPERATURE_STEP to compensate if a slow update is coincidental
    if (tempDiff > TEMPERATURE_STEP) {
        cancel_all_timers();
        quick_adjust_timer = new QTimer(this);
        quick_adjust_timer->setSingleShot(false);
        connect(quick_adjust_timer, &QTimer::timeout, this, &night_color_manager::quick_adjust);

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

    if (current_temp < targetTemp) {
        nextTemp = qMin(current_temp + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(current_temp - TEMPERATURE_STEP, targetTemp);
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

    if (!m_running || quick_adjust_timer) {
        // only reenable the slow update start timer when quick adjust is not active anymore
        return;
    }

    // There is no need for starting the slow update timer. Screen color temperature
    // will be constant all the time now.
    if (m_mode == night_color_mode::constant) {
        return;
    }

    // set up the next slow update
    slow_update_start_timer = new QTimer(this);
    slow_update_start_timer->setSingleShot(true);
    connect(slow_update_start_timer,
            &QTimer::timeout,
            this,
            &night_color_manager::reset_slow_update_start_timer);

    update_transition_timings(false);
    update_target_temperature();

    const int diff = QDateTime::currentDateTime().msecsTo(next_transition.first);
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
    const bool isDay = daylight();
    const int targetTemp = isDay ? day_target_temp : night_target_temp;

    // We've reached the target color temperature or the transition time is zero.
    if (prev_transition.first == prev_transition.second || current_temp == targetTemp) {
        commit_gamma_ramps(targetTemp);
        return;
    }

    if (prev_transition.first <= now && now <= prev_transition.second) {
        int availTime = now.msecsTo(prev_transition.second);
        slow_update_timer = new QTimer(this);
        slow_update_timer->setSingleShot(false);
        if (isDay) {
            connect(slow_update_timer, &QTimer::timeout, this, [this]() {
                slow_update(day_target_temp);
            });
        } else {
            connect(slow_update_timer, &QTimer::timeout, this, [this]() {
                slow_update(night_target_temp);
            });
        }

        // calculate interval such as temperature is changed by TEMPERATURE_STEP K per timer timeout
        int interval = availTime * TEMPERATURE_STEP / qAbs(targetTemp - current_temp);
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
    if (current_temp < targetTemp) {
        nextTemp = qMin(current_temp + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(current_temp - TEMPERATURE_STEP, targetTemp);
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
    const int targetTemperature
        = mode() != night_color_mode::constant && daylight() ? day_target_temp : night_target_temp;

    if (target_temp == targetTemperature) {
        return;
    }

    target_temp = targetTemperature;

    Q_EMIT target_temperature_changed();
}

void night_color_manager::update_transition_timings(bool force)
{
    if (m_mode == night_color_mode::constant) {
        next_transition = DateTimes();
        prev_transition = DateTimes();
        Q_EMIT previous_transition_timings_changed();
        Q_EMIT scheduled_transition_timings_changed();
        return;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    if (m_mode == night_color_mode::timings) {
        const QDateTime morB = QDateTime(todayNow.date(), morning_time);
        const QDateTime morE = morB.addSecs(transition_time * 60);
        const QDateTime eveB = QDateTime(todayNow.date(), evening_time);
        const QDateTime eveE = eveB.addSecs(transition_time * 60);

        if (morB <= todayNow && todayNow < eveB) {
            next_transition = DateTimes(eveB, eveE);
            prev_transition = DateTimes(morB, morE);
        } else if (todayNow < morB) {
            next_transition = DateTimes(morB, morE);
            prev_transition = DateTimes(eveB.addDays(-1), eveE.addDays(-1));
        } else {
            next_transition = DateTimes(morB.addDays(1), morE.addDays(1));
            prev_transition = DateTimes(eveB, eveE);
        }
        Q_EMIT previous_transition_timings_changed();
        Q_EMIT scheduled_transition_timings_changed();
        return;
    }

    double lat, lng;
    if (m_mode == night_color_mode::automatic) {
        lat = lat_auto;
        lng = lng_auto;
    } else {
        lat = lat_fixed;
        lng = lng_fixed;
    }

    if (!force) {
        // first try by only switching the timings
        if (daylight()) {
            // next is morning
            prev_transition = next_transition;
            next_transition = get_sun_timings(todayNow.addDays(1), lat, lng, true);
        } else {
            // next is evening
            prev_transition = next_transition;
            next_transition = get_sun_timings(todayNow, lat, lng, false);
        }
    }

    if (force || !check_automatic_sun_timings()) {
        // in case this fails, reset them
        DateTimes morning = get_sun_timings(todayNow, lat, lng, true);
        if (todayNow < morning.first) {
            prev_transition = get_sun_timings(todayNow.addDays(-1), lat, lng, false);
            next_transition = morning;
        } else {
            DateTimes evening = get_sun_timings(todayNow, lat, lng, false);
            if (todayNow < evening.first) {
                prev_transition = morning;
                next_transition = evening;
            } else {
                prev_transition = evening;
                next_transition = get_sun_timings(todayNow.addDays(1), lat, lng, true);
            }
        }
    }

    Q_EMIT previous_transition_timings_changed();
    Q_EMIT scheduled_transition_timings_changed();
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
    if (prev_transition.first.isValid() && prev_transition.second.isValid()
        && next_transition.first.isValid() && next_transition.second.isValid()) {
        const QDateTime todayNow = QDateTime::currentDateTime();
        return prev_transition.first <= todayNow && todayNow < next_transition.first
            && prev_transition.first.msecsTo(next_transition.first) < MSC_DAY * 23. / 24;
    }
    return false;
}

bool night_color_manager::daylight() const
{
    return prev_transition.first.date() == next_transition.first.date();
}

int night_color_manager::current_target_temp() const
{
    if (!m_running) {
        return NEUTRAL_TEMPERATURE;
    }

    if (m_mode == night_color_mode::constant) {
        return night_target_temp;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    auto f = [this, todayNow](int target1, int target2) {
        if (todayNow <= prev_transition.second) {
            double residueQuota = todayNow.msecsTo(prev_transition.second)
                / (double)prev_transition.first.msecsTo(prev_transition.second);

            double ret
                = (int)((1. - residueQuota) * (double)target2 + residueQuota * (double)target1);
            // remove single digits
            ret = ((int)(0.1 * ret)) * 10;
            return (int)ret;
        } else {
            return target2;
        }
    };

    if (daylight()) {
        return f(night_target_temp, day_target_temp);
    } else {
        return f(day_target_temp, night_target_temp);
    }
}

void night_color_manager::commit_gamma_ramps(int temperature)
{
    const auto outs = kwinApp()->get_base().get_outputs();

    for (auto* o : outs) {
        auto rampsize = o->gamma_ramp_size();
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
            uint16_t value = (double)i / rampsize * (UINT16_MAX + 1);
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

        if (o->set_gamma_ramp(ramp)) {
            set_current_temperature(temperature);
            failed_commit_attempts = 0;
        } else {
            failed_commit_attempts++;
            if (failed_commit_attempts < 10) {
                qCWarning(KWIN_CORE).nospace()
                    << "Committing Gamma Ramp failed for output " << o->name() << ". Trying "
                    << (10 - failed_commit_attempts) << " times more.";
            } else {
                // TODO: On multi monitor setups we could try to rollback earlier changes for
                // already committed outputs
                qCWarning(KWIN_CORE)
                    << "Gamma Ramp commit failed too often. Deactivating color correction for now.";
                failed_commit_attempts = 0; // reset so we can try again later (i.e. after suspend
                                            // phase or config change)
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
    if (qAbs(lat_auto - latitude) < 2 && qAbs(lng_auto - longitude) < 1) {
        return;
    }
    cancel_all_timers();
    lat_auto = latitude;
    lng_auto = longitude;

    Settings* s = Settings::self();
    s->setLatitudeAuto(latitude);
    s->setLongitudeAuto(longitude);
    s->save();

    reset_all_timers();
}

void night_color_manager::set_enabled(bool enable)
{
    if (enabled == enable) {
        return;
    }
    enabled = enable;
    clock_skew_notifier->set_active(enabled);
    Q_EMIT enabled_changed();
}

void night_color_manager::set_running(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;
    Q_EMIT runningChanged();
}

void night_color_manager::set_current_temperature(int temperature)
{
    if (current_temp == temperature) {
        return;
    }
    current_temp = temperature;
    Q_EMIT current_temperature_changed();
}

void night_color_manager::set_mode(night_color_mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    Q_EMIT mode_changed();
}

}
