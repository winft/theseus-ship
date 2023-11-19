/*
SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "render/types.h"

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <functional>

namespace KWin::render::post
{

struct night_color_data;

struct color_correct_dbus_integration {
    color_correct_dbus_integration(std::function<void(bool)> inhibit,
                                   std::function<void(double, double)> loc_update,
                                   night_color_data const& data)
        : inhibit{inhibit}
        , loc_update{loc_update}
        , data{data}
    {
    }

    std::function<void(bool)> inhibit;
    std::function<void(double, double)> loc_update;
    night_color_data const& data;
};

class KWIN_EXPORT color_correct_dbus_interface : public QObject, public QDBusContext
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
    explicit color_correct_dbus_interface(color_correct_dbus_integration integration);
    ~color_correct_dbus_interface() override;

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

    color_correct_dbus_integration integration;
    QDBusServiceWatcher* m_inhibitorWatcher;
    QMultiHash<QString, uint> m_inhibitors;
    uint m_lastInhibitionCookie = 0;
};

}
