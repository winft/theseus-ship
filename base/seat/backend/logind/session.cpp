/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "session.h"

#include "base/logging.h"
#include "config-kwin.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDebug>

#include <sys/stat.h>
#if HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#ifndef major
#include <sys/types.h>
#endif
#include <fcntl.h>
#include <unistd.h>

struct DBusLogindSeat {
    QString name;
    QDBusObjectPath path;
};

QDBusArgument& operator<<(QDBusArgument& argument, const DBusLogindSeat& seat)
{
    argument.beginStructure();
    argument << seat.name << seat.path;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusLogindSeat& seat)
{
    argument.beginStructure();
    argument >> seat.name >> seat.path;
    argument.endStructure();
    return argument;
}

Q_DECLARE_METATYPE(DBusLogindSeat)

namespace KWin::base::seat::backend::logind
{

static QString const s_login1Name = QStringLiteral("logind");
static QString const s_login1Service = QStringLiteral("org.freedesktop.login1");
static QString const s_login1Path = QStringLiteral("/org/freedesktop/login1");
static QString const s_login1ManagerInterface = QStringLiteral("org.freedesktop.login1.Manager");
static QString const s_login1SeatInterface = QStringLiteral("org.freedesktop.login1.Seat");
static QString const s_login1SessionInterface = QStringLiteral("org.freedesktop.login1.Session");
static QString const s_login1ActiveProperty = QStringLiteral("Active");

static QString const s_ck2Name = QStringLiteral("ConsoleKit");
static QString const s_ck2Service = QStringLiteral("org.freedesktop.ConsoleKit");
static QString const s_ck2Path = QStringLiteral("/org/freedesktop/ConsoleKit/Manager");
static QString const s_ck2ManagerInterface = QStringLiteral("org.freedesktop.ConsoleKit.Manager");
static QString const s_ck2SeatInterface = QStringLiteral("org.freedesktop.ConsoleKit.Seat");
static QString const s_ck2SessionInterface = QStringLiteral("org.freedesktop.ConsoleKit.Session");
static QString const s_ck2ActiveProperty = QStringLiteral("active");

static QString const s_dbusPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

session::session(QDBusConnection const& connection)
    : seat::session()
    , m_bus(connection)
    , m_connected(false)
    , m_sessionControl(false)
    , m_sessionActive(false)
{
    // check whether the logind service is registered
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                          QStringLiteral("/"),
                                                          QStringLiteral("org.freedesktop.DBus"),
                                                          QStringLiteral("ListNames"));
    QDBusPendingReply<QStringList> async = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* callWatcher = new QDBusPendingCallWatcher(async, this);
    connect(callWatcher,
            &QDBusPendingCallWatcher::finished,
            this,
            [this](QDBusPendingCallWatcher* self) {
                QDBusPendingReply<QStringList> reply = *self;
                self->deleteLater();
                if (!reply.isValid()) {
                    return;
                }
                if (reply.value().contains(s_login1Service)) {
                    setupSessionController(SessionControllerLogind);
                } else if (reply.value().contains(s_ck2Service)) {
                    setupSessionController(SessionControllerConsoleKit);
                }
            });
}

session::session()
    : session(QDBusConnection::systemBus())
{
}

void session::setupSessionController(SessionController controller)
{
    if (controller == SessionControllerLogind) {
        // We have the logind serivce, set it up and use it
        m_sessionControllerName = s_login1Name;
        m_sessionControllerService = s_login1Service;
        m_sessionControllerPath = s_login1Path;
        m_sessionControllerManagerInterface = s_login1ManagerInterface;
        m_sessionControllerSeatInterface = s_login1SeatInterface;
        m_sessionControllerSessionInterface = s_login1SessionInterface;
        m_sessionControllerActiveProperty = s_login1ActiveProperty;
        m_logindServiceWatcher = new QDBusServiceWatcher(
            m_sessionControllerService,
            m_bus,
            QDBusServiceWatcher::WatchForUnregistration | QDBusServiceWatcher::WatchForRegistration,
            this);
        connect(m_logindServiceWatcher,
                &QDBusServiceWatcher::serviceRegistered,
                this,
                &session::logindServiceRegistered);
        connect(m_logindServiceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
            m_connected = false;
            Q_EMIT connectedChanged();
        });
        logindServiceRegistered();
    } else if (controller == SessionControllerConsoleKit) {
        // We have the ConsoleKit serivce, set it up and use it
        m_sessionControllerName = s_ck2Name;
        m_sessionControllerService = s_ck2Service;
        m_sessionControllerPath = s_ck2Path;
        m_sessionControllerManagerInterface = s_ck2ManagerInterface;
        m_sessionControllerSeatInterface = s_ck2SeatInterface;
        m_sessionControllerSessionInterface = s_ck2SessionInterface;
        m_sessionControllerActiveProperty = s_ck2ActiveProperty;
        m_logindServiceWatcher = new QDBusServiceWatcher(
            m_sessionControllerService,
            m_bus,
            QDBusServiceWatcher::WatchForUnregistration | QDBusServiceWatcher::WatchForRegistration,
            this);
        connect(m_logindServiceWatcher,
                &QDBusServiceWatcher::serviceRegistered,
                this,
                &session::logindServiceRegistered);
        connect(m_logindServiceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
            m_connected = false;
            Q_EMIT connectedChanged();
        });
        logindServiceRegistered();
    }
}

void session::logindServiceRegistered()
{
    const QByteArray sessionId = qgetenv("XDG_SESSION_ID");
    QString methodName;
    QVariantList args;
    if (sessionId.isEmpty()) {
        methodName = QStringLiteral("GetSessionByPID");
        args << (quint32)QCoreApplication::applicationPid();
    } else {
        methodName = QStringLiteral("GetSession");
        args << QString::fromLocal8Bit(sessionId);
    }
    // get the current session
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionControllerPath,
                                                          m_sessionControllerManagerInterface,
                                                          methodName);
    message.setArguments(args);
    QDBusPendingReply<QDBusObjectPath> session = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(session, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* self) {
            QDBusPendingReply<QDBusObjectPath> reply = *self;
            self->deleteLater();
            if (m_connected) {
                return;
            }
            if (!reply.isValid()) {
                qCDebug(KWIN_CORE) << "The session is not registered with "
                                   << m_sessionControllerName << " " << reply.error().message();
                return;
            }
            m_sessionPath = reply.value().path();
            qCDebug(KWIN_CORE) << "Session path:" << m_sessionPath;
            m_connected = true;
            connectSessionPropertiesChanged();
            // activate the session, in case we are not on it
            QDBusMessage message
                = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                 m_sessionPath,
                                                 m_sessionControllerSessionInterface,
                                                 QStringLiteral("Activate"));
            // blocking on purpose
            m_bus.call(message);
            getSeat();
            getSessionActive();
            getVirtualTerminal();

            Q_EMIT connectedChanged();
        });
}

void session::connectSessionPropertiesChanged()
{
    m_bus.connect(m_sessionControllerService,
                  m_sessionPath,
                  s_dbusPropertiesInterface,
                  QStringLiteral("PropertiesChanged"),
                  this,
                  SLOT(getSessionActive()));
    m_bus.connect(m_sessionControllerService,
                  m_sessionPath,
                  s_dbusPropertiesInterface,
                  QStringLiteral("PropertiesChanged"),
                  this,
                  SLOT(getVirtualTerminal()));
}

void session::getSessionActive()
{
    if (!m_connected || m_sessionPath.isEmpty()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          s_dbusPropertiesInterface,
                                                          QStringLiteral("Get"));
    message.setArguments(
        QVariantList({m_sessionControllerSessionInterface, m_sessionControllerActiveProperty}));
    QDBusPendingReply<QVariant> reply = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(reply, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* self) {
            QDBusPendingReply<QVariant> reply = *self;
            self->deleteLater();
            if (!reply.isValid()) {
                qCDebug(KWIN_CORE) << "Failed to get Active Property of " << m_sessionControllerName
                                   << " session:" << reply.error().message();
                return;
            }
            const bool active = reply.value().toBool();
            if (m_sessionActive != active) {
                m_sessionActive = active;
                Q_EMIT sessionActiveChanged(m_sessionActive);
            }
        });
}

void session::getVirtualTerminal()
{
    if (!m_connected || m_sessionPath.isEmpty()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          s_dbusPropertiesInterface,
                                                          QStringLiteral("Get"));
    message.setArguments(
        QVariantList({m_sessionControllerSessionInterface, QStringLiteral("VTNr")}));
    QDBusPendingReply<QVariant> reply = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(reply, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* self) {
            QDBusPendingReply<QVariant> reply = *self;
            self->deleteLater();
            if (!reply.isValid()) {
                qCDebug(KWIN_CORE) << "Failed to get VTNr Property of " << m_sessionControllerName
                                   << " session:" << reply.error().message();
                return;
            }
            const int vt = reply.value().toUInt();
            if (m_vt != (int)vt) {
                m_vt = vt;
                Q_EMIT virtualTerminalChanged(m_vt);
            }
        });
}

void session::take_control()
{
    if (!m_connected || m_sessionPath.isEmpty() || m_sessionControl) {
        return;
    }
    static bool s_recursionCheck = false;
    if (s_recursionCheck) {
        return;
    }
    s_recursionCheck = true;

    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          m_sessionControllerSessionInterface,
                                                          QStringLiteral("TakeControl"));
    message.setArguments(QVariantList({QVariant(false)}));
    QDBusPendingReply<void> session = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(session, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* self) {
            s_recursionCheck = false;
            QDBusPendingReply<void> reply = *self;
            self->deleteLater();
            if (!reply.isValid()) {
                qCDebug(KWIN_CORE) << "Failed to get session control" << reply.error().message();
                return;
            }
            qCDebug(KWIN_CORE) << "Gained session control";
            m_sessionControl = true;
            m_bus.connect(m_sessionControllerService,
                          m_sessionPath,
                          m_sessionControllerSessionInterface,
                          QStringLiteral("PauseDevice"),
                          this,
                          SLOT(pauseDevice(uint, uint, QString)));
        });
}

void session::release_control()
{
    if (!m_connected || m_sessionPath.isEmpty() || !m_sessionControl) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          m_sessionControllerSessionInterface,
                                                          QStringLiteral("ReleaseControl"));
    m_bus.asyncCall(message);
    m_sessionControl = false;
}

int session::takeDevice(const char* path)
{
    struct stat st;
    if (stat(path, &st) < 0) {
        qCDebug(KWIN_CORE) << "Could not stat the path";
        return -1;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          m_sessionControllerSessionInterface,
                                                          QStringLiteral("TakeDevice"));
    message.setArguments(QVariantList({QVariant(major(st.st_rdev)), QVariant(minor(st.st_rdev))}));
    // intended to be a blocking call
    QDBusMessage reply = m_bus.call(message);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCDebug(KWIN_CORE) << "Could not take device" << path
                           << ", cause: " << reply.errorMessage();
        return -1;
    }

    // The dup syscall removes the CLOEXEC flag as a side-effect. So use fcntl's F_DUPFD_CLOEXEC
    // cmd.
    return fcntl(reply.arguments().first().value<QDBusUnixFileDescriptor>().fileDescriptor(),
                 F_DUPFD_CLOEXEC,
                 0);
}

void session::releaseDevice(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) {
        qCDebug(KWIN_CORE) << "Could not stat the file descriptor";
    } else {
        QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                              m_sessionPath,
                                                              m_sessionControllerSessionInterface,
                                                              QStringLiteral("ReleaseDevice"));
        message.setArguments(
            QVariantList({QVariant(major(st.st_rdev)), QVariant(minor(st.st_rdev))}));
        m_bus.asyncCall(message);
    }
    close(fd);
}

void session::pauseDevice(uint devMajor, uint devMinor, const QString& type)
{
    if (QString::compare(type, QStringLiteral("pause"), Qt::CaseInsensitive) == 0) {
        // unconditionally call complete
        QDBusMessage message
            = QDBusMessage::createMethodCall(m_sessionControllerService,
                                             m_sessionPath,
                                             m_sessionControllerSessionInterface,
                                             QStringLiteral("PauseDeviceComplete"));
        message.setArguments(QVariantList({QVariant(devMajor), QVariant(devMinor)}));
        m_bus.asyncCall(message);
    }
}

void session::getSeat()
{
    if (m_sessionPath.isEmpty()) {
        return;
    }
    qDBusRegisterMetaType<DBusLogindSeat>();
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_sessionPath,
                                                          s_dbusPropertiesInterface,
                                                          QStringLiteral("Get"));
    message.setArguments(
        QVariantList({m_sessionControllerSessionInterface, QStringLiteral("Seat")}));
    QDBusPendingReply<QVariant> reply = m_bus.asyncCall(message);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(reply, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* self) {
            QDBusPendingReply<QVariant> reply = *self;
            self->deleteLater();
            if (!reply.isValid()) {
                qCDebug(KWIN_CORE) << "Failed to get Seat Property of " << m_sessionControllerName
                                   << " session:" << reply.error().message();
                return;
            }
            DBusLogindSeat seat = qdbus_cast<DBusLogindSeat>(reply.value().value<QDBusArgument>());
            const QString seatPath = seat.path.path();
            qCDebug(KWIN_CORE) << m_sessionControllerName << " seat:" << seat.name << "/"
                               << seatPath;
            m_seatPath = seatPath;
            m_seatName = seat.name;
        });
}

void session::switchVirtualTerminal(quint32 vtNr)
{
    if (!m_connected || m_seatPath.isEmpty()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(m_sessionControllerService,
                                                          m_seatPath,
                                                          m_sessionControllerSeatInterface,
                                                          QStringLiteral("SwitchTo"));
    message.setArguments(QVariantList{vtNr});
    m_bus.asyncCall(message);
}

}
