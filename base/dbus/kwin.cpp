/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin.h"

#include "debug/console/console.h"
#include "debug/perf/ftrace.h"
#include "kwinadaptor.h"
#include "main.h"
#include "toplevel.h"
#include "win/control.h"
#include "win/geo.h"
#include "win/placement.h"
#include "workspace.h"
#include <input/platform.h>

#include <QDBusServiceWatcher>

namespace KWin::base::dbus
{

kwin::kwin(QObject* parent)
    : QObject(parent)
    , m_serviceName(QStringLiteral("org.kde.KWin"))
{
    (void)new KWinAdaptor(this);

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/KWin"), this);

    auto const dBusSuffix = qgetenv("KWIN_DBUS_SERVICE_SUFFIX");
    if (!dBusSuffix.isNull()) {
        m_serviceName = m_serviceName + QLatin1Char('.') + dBusSuffix;
    }

    if (!dbus.registerService(m_serviceName)) {
        QDBusServiceWatcher* dog = new QDBusServiceWatcher(
            m_serviceName, dbus, QDBusServiceWatcher::WatchForUnregistration, this);
        connect(dog, &QDBusServiceWatcher::serviceUnregistered, this, &kwin::becomeKWinService);
    }

    dbus.connect(QString(),
                 QStringLiteral("/KWin"),
                 QStringLiteral("org.kde.KWin"),
                 QStringLiteral("reloadConfig"),
                 Workspace::self(),
                 SLOT(slotReloadConfig()));
}

void kwin::becomeKWinService(const QString& service)
{
    // TODO: this watchdog exists to make really safe that we at some point get the service
    // but it's probably no longer needed since we explicitly unregister the service with the
    // deconstructor
    if (service == m_serviceName && QDBusConnection::sessionBus().registerService(m_serviceName)
        && sender()) {
        sender()->deleteLater();
    }
}

kwin::~kwin()
{
    QDBusConnection::sessionBus().unregisterService(m_serviceName);

    // KApplication automatically also grabs org.kde.kwin, so it's often been used externally -
    // ensure to free it as well
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.kwin"));
}

void kwin::reconfigure()
{
    Workspace::self()->reconfigure();
}

void kwin::killWindow()
{
    Workspace::self()->slotKillWindow();
}

void kwin::unclutterDesktop()
{
    win::unclutter_desktop();
}

QString kwin::supportInformation()
{
    return Workspace::self()->supportInformation();
}

bool kwin::startActivity(const QString& /*in0*/)
{
    return false;
}

bool kwin::stopActivity(const QString& /*in0*/)
{
    return false;
}

int kwin::currentDesktop()
{
    return win::virtual_desktop_manager::self()->current();
}

bool kwin::setCurrentDesktop(int desktop)
{
    return win::virtual_desktop_manager::self()->setCurrent(desktop);
}

void kwin::nextDesktop()
{
    win::virtual_desktop_manager::self()->moveTo<win::virtual_desktop_next>();
}

void kwin::previousDesktop()
{
    win::virtual_desktop_manager::self()->moveTo<win::virtual_desktop_previous>();
}

void kwin::showDebugConsole()
{
    auto console = kwinApp()->create_debug_console();
    console->show();
}

void kwin::enableFtrace(bool enable)
{
    const QString name = QStringLiteral("org.kde.kwin.enableFtrace");
#if HAVE_PERF
    if (!Perf::Ftrace::valid()) {
        const QString msg = QStringLiteral("Ftrace marker not available");
        QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
        return;
    }
    if (!Perf::Ftrace::setEnabled(enable)) {
        const QString msg = QStringLiteral("Ftrace marker is available but could not be ")
                                .append(enable ? "enabled" : "disabled");
        QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
    }
    return;
#else
    Q_UNUSED(enable)
    const QString msg = QStringLiteral("KWin built without ftrace marking capability");
    QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
#endif
}

namespace
{

QVariantMap clientToVariantMap(Toplevel const* c)
{
    return {{QStringLiteral("resourceClass"), c->resourceClass()},
            {QStringLiteral("resourceName"), c->resourceName()},
            {QStringLiteral("desktopFile"), c->control->desktop_file_name()},
            {QStringLiteral("role"), c->windowRole()},
            {QStringLiteral("caption"), c->caption.normal},
            {QStringLiteral("clientMachine"), c->wmClientMachine(true)},
            {QStringLiteral("localhost"), c->isLocalhost()},
            {QStringLiteral("type"), c->windowType()},
            {QStringLiteral("x"), c->pos().x()},
            {QStringLiteral("y"), c->pos().y()},
            {QStringLiteral("width"), c->size().width()},
            {QStringLiteral("height"), c->size().height()},
            {QStringLiteral("x11DesktopNumber"), c->desktop()},
            {QStringLiteral("minimized"), c->control->minimized()},
            {QStringLiteral("shaded"), false},
            {QStringLiteral("fullscreen"), c->control->fullscreen()},
            {QStringLiteral("keepAbove"), c->control->keep_above()},
            {QStringLiteral("keepBelow"), c->control->keep_below()},
            {QStringLiteral("noBorder"), c->noBorder()},
            {QStringLiteral("skipTaskbar"), c->control->skip_taskbar()},
            {QStringLiteral("skipPager"), c->control->skip_pager()},
            {QStringLiteral("skipSwitcher"), c->control->skip_switcher()},
            {QStringLiteral("maximizeHorizontal"),
             static_cast<int>(c->maximizeMode() & win::maximize_mode::horizontal)},
            {QStringLiteral("maximizeVertical"),
             static_cast<int>(c->maximizeMode() & win::maximize_mode::vertical)}};
}

}

QVariantMap kwin::queryWindowInfo()
{
    m_replyQueryWindowInfo = message();
    setDelayedReply(true);

    kwinApp()->input->start_interactive_window_selection([this](Toplevel* t) {
        if (!t) {
            QDBusConnection::sessionBus().send(m_replyQueryWindowInfo.createErrorReply(
                QStringLiteral("org.kde.KWin.Error.UserCancel"),
                QStringLiteral("User cancelled the query")));
            return;
        }
        if (!t->control) {
            QDBusConnection::sessionBus().send(m_replyQueryWindowInfo.createErrorReply(
                QStringLiteral("org.kde.KWin.Error.InvalidWindow"),
                QStringLiteral("Tried to query information about an unmanaged window")));
            return;
        }
        QDBusConnection::sessionBus().send(
            m_replyQueryWindowInfo.createReply(clientToVariantMap(t)));
    });

    return QVariantMap{};
}

QVariantMap kwin::getWindowInfo(const QString& uuid)
{
    auto const id = QUuid::fromString(uuid);
    auto const client = workspace()->findAbstractClient(
        [&id](Toplevel const* c) { return c->internalId() == id; });

    if (client) {
        return clientToVariantMap(client);
    } else {
        return {};
    }
}

}
