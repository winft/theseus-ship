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
#include "manager.h"

#include "clockskewnotifier.h"
#include "colorcorrectdbusinterface.h"
#include "suncalc.h"
#include <colorcorrect_logging.h>

#include "base/gamma_ramp.h"
#include "base/platform.h"
#include <base/output.h>
#include <main.h>
#include <platform.h>
#include <screens.h>
#include <seat/session.h>
#include <workspace.h>

#include <colorcorrect_settings.h>

#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QDBusConnection>
#include <QTimer>

namespace KWin::ColorCorrect
{

static const int QUICK_ADJUST_DURATION = 2000;
static const int TEMPERATURE_STEP = 50;

static bool checkLocation(double lat, double lng)
{
    return -90 <= lat && lat <= 90 && -180 <= lng && lng <= 180;
}

Manager::Manager(QObject* parent)
    : QObject(parent)
{
    m_iface = new ColorCorrectDBusInterface(this);
    m_skewNotifier = new ClockSkewNotifier(this);

    connect(kwinApp(), &Application::startup_finished, this, &Manager::init);

    // Display a message when Night Color is (un)inhibited.
    connect(this, &Manager::inhibitedChanged, this, [this] {
        // TODO: Maybe use different icons?
        const QString iconName = isInhibited()
            ? QStringLiteral("preferences-desktop-display-nightcolor-off")
            : QStringLiteral("preferences-desktop-display-nightcolor-on");

        const QString text = isInhibited() ? i18nc("Night Color was disabled", "Night Color Off")
                                           : i18nc("Night Color was enabled", "Night Color On");

        QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                              QStringLiteral("/org/kde/osdService"),
                                                              QStringLiteral("org.kde.osdService"),
                                                              QStringLiteral("showText"));
        message.setArguments({iconName, text});

        QDBusConnection::sessionBus().asyncCall(message);
    });
}

void Manager::init()
{
    Settings::instance(kwinApp()->config());

    m_configWatcher = KConfigWatcher::create(kwinApp()->config());
    QObject::connect(
        m_configWatcher.data(), &KConfigWatcher::configChanged, this, &Manager::reconfigure);

    // we may always read in the current config
    readConfig();

    if (!isAvailable()) {
        return;
    }

    connect(&kwinApp()->get_base().screens, &Screens::countChanged, this, &Manager::hardReset);

    connect(
        kwinApp()->session.get(), &seat::session::sessionActiveChanged, this, [this](bool active) {
            if (active) {
                hardReset();
            } else {
                cancelAllTimers();
            }
        });

    connect(m_skewNotifier, &ClockSkewNotifier::clockSkewed, this, [this]() {
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
            qCDebug(KWIN_COLORCORRECTION)
                << "Failed to get PreparingForSleep Property of logind session:"
                << reply.error().message();
            // Always do a hard reset in case we have no further information.
            comingFromSuspend = true;
        }

        if (comingFromSuspend) {
            hardReset();
        } else {
            resetAllTimers();
        }
    });

    hardReset();
}

void Manager::hardReset()
{
    cancelAllTimers();

    updateTransitionTimings(true);
    updateTargetTemperature();

    if (isAvailable() && isEnabled() && !isInhibited()) {
        setRunning(true);
        commitGammaRamps(currentTargetTemp());
    }
    resetAllTimers();
}

void Manager::reconfigure()
{
    cancelAllTimers();
    readConfig();
    resetAllTimers();
}

void Manager::toggle()
{
    m_isGloballyInhibited = !m_isGloballyInhibited;
    m_isGloballyInhibited ? inhibit() : uninhibit();
}

bool Manager::isInhibited() const
{
    return m_inhibitReferenceCount;
}

void Manager::inhibit()
{
    m_inhibitReferenceCount++;

    if (m_inhibitReferenceCount == 1) {
        resetAllTimers();
        Q_EMIT inhibitedChanged();
    }
}

void Manager::uninhibit()
{
    m_inhibitReferenceCount--;

    if (!m_inhibitReferenceCount) {
        resetAllTimers();
        Q_EMIT inhibitedChanged();
    }
}

bool Manager::isEnabled() const
{
    return m_active;
}

bool Manager::isRunning() const
{
    return m_running;
}

bool Manager::isAvailable() const
{
    // TODO(romangg): That depended in the past on the hardware backend in use. But right now all
    //                backends support gamma control. We might remove this in the future.
    return true;
}

int Manager::currentTemperature() const
{
    return m_currentTemp;
}

int Manager::targetTemperature() const
{
    return m_targetTemperature;
}

NightColorMode Manager::mode() const
{
    return m_mode;
}

QDateTime Manager::previousTransitionDateTime() const
{
    return m_prev.first;
}

qint64 Manager::previousTransitionDuration() const
{
    return m_prev.first.msecsTo(m_prev.second);
}

QDateTime Manager::scheduledTransitionDateTime() const
{
    return m_next.first;
}

qint64 Manager::scheduledTransitionDuration() const
{
    return m_next.first.msecsTo(m_next.second);
}

void Manager::initShortcuts()
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
        QKeySequence(), toggleAction, this, &Manager::toggle);
}

void Manager::readConfig()
{
    Settings* s = Settings::self();
    s->load();

    setEnabled(s->active());

    const NightColorMode mode = s->mode();
    switch (s->mode()) {
    case NightColorMode::Automatic:
    case NightColorMode::Location:
    case NightColorMode::Timings:
    case NightColorMode::Constant:
        setMode(mode);
        break;
    default:
        // Fallback for invalid setting values.
        setMode(NightColorMode::Automatic);
        break;
    }

    m_nightTargetTemp = qBound(MIN_TEMPERATURE, s->nightTemperature(), NEUTRAL_TEMPERATURE);

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
    m_latAuto = lat;
    m_lngAuto = lng;
    // fixed location
    lat = s->latitudeFixed();
    lng = s->longitudeFixed();
    correctReadin();
    m_latFixed = lat;
    m_lngFixed = lng;

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
    m_morning = mrB;
    m_evening = evB;
    m_trTime = qMax(trTime / 1000 / 60, 1);
}

void Manager::resetAllTimers()
{
    cancelAllTimers();
    if (isAvailable()) {
        setRunning(isEnabled() && !isInhibited());
        // we do this also for active being false in order to reset the temperature back to the day
        // value
        resetQuickAdjustTimer();
    } else {
        setRunning(false);
    }
}

void Manager::cancelAllTimers()
{
    delete m_slowUpdateStartTimer;
    delete m_slowUpdateTimer;
    delete m_quickAdjustTimer;

    m_slowUpdateStartTimer = nullptr;
    m_slowUpdateTimer = nullptr;
    m_quickAdjustTimer = nullptr;
}

void Manager::resetQuickAdjustTimer()
{
    updateTransitionTimings(false);
    updateTargetTemperature();

    int tempDiff = qAbs(currentTargetTemp() - m_currentTemp);
    // allow tolerance of one TEMPERATURE_STEP to compensate if a slow update is coincidental
    if (tempDiff > TEMPERATURE_STEP) {
        cancelAllTimers();
        m_quickAdjustTimer = new QTimer(this);
        m_quickAdjustTimer->setSingleShot(false);
        connect(m_quickAdjustTimer, &QTimer::timeout, this, &Manager::quickAdjust);

        int interval = QUICK_ADJUST_DURATION / (tempDiff / TEMPERATURE_STEP);
        if (interval == 0) {
            interval = 1;
        }
        m_quickAdjustTimer->start(interval);
    } else {
        resetSlowUpdateStartTimer();
    }
}

void Manager::quickAdjust()
{
    if (!m_quickAdjustTimer) {
        return;
    }

    int nextTemp;
    const int targetTemp = currentTargetTemp();

    if (m_currentTemp < targetTemp) {
        nextTemp = qMin(m_currentTemp + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(m_currentTemp - TEMPERATURE_STEP, targetTemp);
    }
    commitGammaRamps(nextTemp);

    if (nextTemp == targetTemp) {
        // stop timer, we reached the target temp
        delete m_quickAdjustTimer;
        m_quickAdjustTimer = nullptr;
        resetSlowUpdateStartTimer();
    }
}

void Manager::resetSlowUpdateStartTimer()
{
    delete m_slowUpdateStartTimer;
    m_slowUpdateStartTimer = nullptr;

    if (!m_running || m_quickAdjustTimer) {
        // only reenable the slow update start timer when quick adjust is not active anymore
        return;
    }

    // There is no need for starting the slow update timer. Screen color temperature
    // will be constant all the time now.
    if (m_mode == NightColorMode::Constant) {
        return;
    }

    // set up the next slow update
    m_slowUpdateStartTimer = new QTimer(this);
    m_slowUpdateStartTimer->setSingleShot(true);
    connect(m_slowUpdateStartTimer, &QTimer::timeout, this, &Manager::resetSlowUpdateStartTimer);

    updateTransitionTimings(false);
    updateTargetTemperature();

    const int diff = QDateTime::currentDateTime().msecsTo(m_next.first);
    if (diff <= 0) {
        qCCritical(KWIN_COLORCORRECTION) << "Error in time calculation. Deactivating Night Color.";
        return;
    }
    m_slowUpdateStartTimer->start(diff);

    // start the current slow update
    resetSlowUpdateTimer();
}

void Manager::resetSlowUpdateTimer()
{
    delete m_slowUpdateTimer;
    m_slowUpdateTimer = nullptr;

    const QDateTime now = QDateTime::currentDateTime();
    const bool isDay = daylight();
    const int targetTemp = isDay ? m_dayTargetTemp : m_nightTargetTemp;

    // We've reached the target color temperature or the transition time is zero.
    if (m_prev.first == m_prev.second || m_currentTemp == targetTemp) {
        commitGammaRamps(targetTemp);
        return;
    }

    if (m_prev.first <= now && now <= m_prev.second) {
        int availTime = now.msecsTo(m_prev.second);
        m_slowUpdateTimer = new QTimer(this);
        m_slowUpdateTimer->setSingleShot(false);
        if (isDay) {
            connect(m_slowUpdateTimer, &QTimer::timeout, this, [this]() {
                slowUpdate(m_dayTargetTemp);
            });
        } else {
            connect(m_slowUpdateTimer, &QTimer::timeout, this, [this]() {
                slowUpdate(m_nightTargetTemp);
            });
        }

        // calculate interval such as temperature is changed by TEMPERATURE_STEP K per timer timeout
        int interval = availTime * TEMPERATURE_STEP / qAbs(targetTemp - m_currentTemp);
        if (interval == 0) {
            interval = 1;
        }
        m_slowUpdateTimer->start(interval);
    }
}

void Manager::slowUpdate(int targetTemp)
{
    if (!m_slowUpdateTimer) {
        return;
    }
    int nextTemp;
    if (m_currentTemp < targetTemp) {
        nextTemp = qMin(m_currentTemp + TEMPERATURE_STEP, targetTemp);
    } else {
        nextTemp = qMax(m_currentTemp - TEMPERATURE_STEP, targetTemp);
    }
    commitGammaRamps(nextTemp);
    if (nextTemp == targetTemp) {
        // stop timer, we reached the target temp
        delete m_slowUpdateTimer;
        m_slowUpdateTimer = nullptr;
    }
}

void Manager::updateTargetTemperature()
{
    const int targetTemperature
        = mode() != NightColorMode::Constant && daylight() ? m_dayTargetTemp : m_nightTargetTemp;

    if (m_targetTemperature == targetTemperature) {
        return;
    }

    m_targetTemperature = targetTemperature;

    Q_EMIT targetTemperatureChanged();
}

void Manager::updateTransitionTimings(bool force)
{
    if (m_mode == NightColorMode::Constant) {
        m_next = DateTimes();
        m_prev = DateTimes();
        Q_EMIT previousTransitionTimingsChanged();
        Q_EMIT scheduledTransitionTimingsChanged();
        return;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    if (m_mode == NightColorMode::Timings) {
        const QDateTime morB = QDateTime(todayNow.date(), m_morning);
        const QDateTime morE = morB.addSecs(m_trTime * 60);
        const QDateTime eveB = QDateTime(todayNow.date(), m_evening);
        const QDateTime eveE = eveB.addSecs(m_trTime * 60);

        if (morB <= todayNow && todayNow < eveB) {
            m_next = DateTimes(eveB, eveE);
            m_prev = DateTimes(morB, morE);
        } else if (todayNow < morB) {
            m_next = DateTimes(morB, morE);
            m_prev = DateTimes(eveB.addDays(-1), eveE.addDays(-1));
        } else {
            m_next = DateTimes(morB.addDays(1), morE.addDays(1));
            m_prev = DateTimes(eveB, eveE);
        }
        Q_EMIT previousTransitionTimingsChanged();
        Q_EMIT scheduledTransitionTimingsChanged();
        return;
    }

    double lat, lng;
    if (m_mode == NightColorMode::Automatic) {
        lat = m_latAuto;
        lng = m_lngAuto;
    } else {
        lat = m_latFixed;
        lng = m_lngFixed;
    }

    if (!force) {
        // first try by only switching the timings
        if (daylight()) {
            // next is morning
            m_prev = m_next;
            m_next = getSunTimings(todayNow.addDays(1), lat, lng, true);
        } else {
            // next is evening
            m_prev = m_next;
            m_next = getSunTimings(todayNow, lat, lng, false);
        }
    }

    if (force || !checkAutomaticSunTimings()) {
        // in case this fails, reset them
        DateTimes morning = getSunTimings(todayNow, lat, lng, true);
        if (todayNow < morning.first) {
            m_prev = getSunTimings(todayNow.addDays(-1), lat, lng, false);
            m_next = morning;
        } else {
            DateTimes evening = getSunTimings(todayNow, lat, lng, false);
            if (todayNow < evening.first) {
                m_prev = morning;
                m_next = evening;
            } else {
                m_prev = evening;
                m_next = getSunTimings(todayNow.addDays(1), lat, lng, true);
            }
        }
    }

    Q_EMIT previousTransitionTimingsChanged();
    Q_EMIT scheduledTransitionTimingsChanged();
}

DateTimes Manager::getSunTimings(const QDateTime& dateTime,
                                 double latitude,
                                 double longitude,
                                 bool morning) const
{
    DateTimes dateTimes = calculateSunTimings(dateTime, latitude, longitude, morning);
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
            const QTime referenceTime = morning ? QTime(6, 0) : QTime(18, 0);
            dateTimes.first = QDateTime(dateTime.date(), referenceTime);
            dateTimes.second = dateTimes.first.addMSecs(FALLBACK_SLOW_UPDATE_TIME);
        }
    }
    return dateTimes;
}

bool Manager::checkAutomaticSunTimings() const
{
    if (m_prev.first.isValid() && m_prev.second.isValid() && m_next.first.isValid()
        && m_next.second.isValid()) {
        const QDateTime todayNow = QDateTime::currentDateTime();
        return m_prev.first <= todayNow && todayNow < m_next.first
            && m_prev.first.msecsTo(m_next.first) < MSC_DAY * 23. / 24;
    }
    return false;
}

bool Manager::daylight() const
{
    return m_prev.first.date() == m_next.first.date();
}

int Manager::currentTargetTemp() const
{
    if (!m_running) {
        return NEUTRAL_TEMPERATURE;
    }

    if (m_mode == NightColorMode::Constant) {
        return m_nightTargetTemp;
    }

    const QDateTime todayNow = QDateTime::currentDateTime();

    auto f = [this, todayNow](int target1, int target2) {
        if (todayNow <= m_prev.second) {
            double residueQuota
                = todayNow.msecsTo(m_prev.second) / (double)m_prev.first.msecsTo(m_prev.second);

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
        return f(m_nightTargetTemp, m_dayTargetTemp);
    } else {
        return f(m_dayTargetTemp, m_nightTargetTemp);
    }
}

void Manager::commitGammaRamps(int temperature)
{
    const auto outs = kwinApp()->platform->outputs();

    for (auto* o : outs) {
        int rampsize = o->gamma_ramp_size();
        base::gamma_ramp ramp(rampsize);

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
            = (1. - alpha) * blackbodyColor[bbCIndex] + alpha * blackbodyColor[bbCIndex + 3];
        whitePoint[1]
            = (1. - alpha) * blackbodyColor[bbCIndex + 1] + alpha * blackbodyColor[bbCIndex + 4];
        whitePoint[2]
            = (1. - alpha) * blackbodyColor[bbCIndex + 2] + alpha * blackbodyColor[bbCIndex + 5];

        for (int i = 0; i < rampsize; i++) {
            red[i] = qreal(red[i]) / (UINT16_MAX + 1) * whitePoint[0] * (UINT16_MAX + 1);
            green[i] = qreal(green[i]) / (UINT16_MAX + 1) * whitePoint[1] * (UINT16_MAX + 1);
            blue[i] = qreal(blue[i]) / (UINT16_MAX + 1) * whitePoint[2] * (UINT16_MAX + 1);
        }

        if (o->set_gamma_ramp(ramp)) {
            setCurrentTemperature(temperature);
            m_failedCommitAttempts = 0;
        } else {
            m_failedCommitAttempts++;
            if (m_failedCommitAttempts < 10) {
                qCWarning(KWIN_COLORCORRECTION).nospace()
                    << "Committing Gamma Ramp failed for output " << o->name() << ". Trying "
                    << (10 - m_failedCommitAttempts) << " times more.";
            } else {
                // TODO: On multi monitor setups we could try to rollback earlier changes for
                // already committed outputs
                qCWarning(KWIN_COLORCORRECTION)
                    << "Gamma Ramp commit failed too often. Deactivating color correction for now.";
                m_failedCommitAttempts = 0; // reset so we can try again later (i.e. after suspend
                                            // phase or config change)
                setRunning(false);
                cancelAllTimers();
            }
        }
    }
}

void Manager::autoLocationUpdate(double latitude, double longitude)
{
    qCDebug(KWIN_COLORCORRECTION, "Received new location (lat: %f, lng: %f)", latitude, longitude);

    if (!checkLocation(latitude, longitude)) {
        return;
    }

    // we tolerate small deviations with minimal impact on sun timings
    if (qAbs(m_latAuto - latitude) < 2 && qAbs(m_lngAuto - longitude) < 1) {
        return;
    }
    cancelAllTimers();
    m_latAuto = latitude;
    m_lngAuto = longitude;

    Settings* s = Settings::self();
    s->setLatitudeAuto(latitude);
    s->setLongitudeAuto(longitude);
    s->save();

    resetAllTimers();
}

void Manager::setEnabled(bool enabled)
{
    if (m_active == enabled) {
        return;
    }
    m_active = enabled;
    m_skewNotifier->set_active(enabled);
    Q_EMIT enabledChanged();
}

void Manager::setRunning(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;
    Q_EMIT runningChanged();
}

void Manager::setCurrentTemperature(int temperature)
{
    if (m_currentTemp == temperature) {
        return;
    }
    m_currentTemp = temperature;
    Q_EMIT currentTemperatureChanged();
}

void Manager::setMode(NightColorMode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    Q_EMIT modeChanged();
}

}
