/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/seat/session.h"

#include <QDBusConnection>

class QDBusServiceWatcher;

namespace KWin::base::seat::backend::logind
{

class KWIN_EXPORT session : public seat::session
{
    Q_OBJECT
public:
    session();

    bool isConnected() const override
    {
        return m_connected;
    }
    bool hasSessionControl() const override
    {
        return m_sessionControl;
    }
    bool isActiveSession() const override
    {
        return m_sessionActive;
    }
    int vt() const override
    {
        return m_vt;
    }
    void switchVirtualTerminal(quint32 vtNr) override;

    void take_control();
    void release_control();

    int takeDevice(const char* path) override;
    void releaseDevice(int fd) override;

    const QString seat() const override
    {
        return m_seatName;
    }

private Q_SLOTS:
    void getSessionActive();
    void getVirtualTerminal();
    void pauseDevice(uint major, uint minor, const QString& type);

private:
    friend class LogindTest;
    /**
     * The DBusConnection argument is needed for the unit test. Logind uses the system bus
     * on which the unit test's fake logind cannot register to. Thus the unit test need to
     * be able to do everything over the session bus. This ctor allows the LogindTest to
     * create a LogindIntegration which listens on the session bus.
     */
    explicit session(QDBusConnection const& connection);

    void logindServiceRegistered();
    void connectSessionPropertiesChanged();

    enum SessionController {
        SessionControllerLogind,
        SessionControllerConsoleKit,
    };
    void setupSessionController(SessionController controller);
    void getSeat();
    QDBusConnection m_bus;
    QDBusServiceWatcher* m_logindServiceWatcher;

    bool m_connected;
    QString m_sessionPath;
    bool m_sessionControl;
    bool m_sessionActive;
    int m_vt = -1;

    QString m_seatName = QStringLiteral("seat0");
    QString m_seatPath;
    QString m_sessionControllerName;
    QString m_sessionControllerService;
    QString m_sessionControllerPath;
    QString m_sessionControllerManagerInterface;
    QString m_sessionControllerSeatInterface;
    QString m_sessionControllerSessionInterface;
    QString m_sessionControllerActiveProperty;
};

}
