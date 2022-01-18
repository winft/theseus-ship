/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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

// own
#include "dbusinterface.h"
#include "compositingadaptor.h"
#include "virtualdesktopmanageradaptor.h"

// kwin
#include "atoms.h"
#include "render/x11/compositor.h"
#include "debug/console.h"
#include <input/platform.h>
#include "main.h"
#include "perf/ftrace.h"
#include "kwinadaptor.h"
#include "render/platform.h"
#include "render/scene.h"
#include "toplevel.h"
#include "win/control.h"
#include "win/geo.h"
#include "win/placement.h"
#include "workspace.h"
#include "virtualdesktops.h"

// Qt
#include <QOpenGLContext>
#include <QDBusServiceWatcher>

namespace KWin
{

DBusInterface::DBusInterface(QObject *parent)
    : QObject(parent)
    , m_serviceName(QStringLiteral("org.kde.KWin"))
{
    (void) new KWinAdaptor(this);

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/KWin"), this);
    const QByteArray dBusSuffix = qgetenv("KWIN_DBUS_SERVICE_SUFFIX");
    if (!dBusSuffix.isNull()) {
        m_serviceName = m_serviceName + QLatin1Char('.') + dBusSuffix;
    }
    if (!dbus.registerService(m_serviceName)) {
        QDBusServiceWatcher *dog = new QDBusServiceWatcher(m_serviceName, dbus, QDBusServiceWatcher::WatchForUnregistration, this);
        connect(dog, &QDBusServiceWatcher::serviceUnregistered, this, &DBusInterface::becomeKWinService);
    } else {
        announceService();
    }
    dbus.connect(QString(), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig"),
                 Workspace::self(), SLOT(slotReloadConfig()));
    connect(kwinApp(), &Application::x11ConnectionChanged, this, &DBusInterface::announceService);
}

void DBusInterface::becomeKWinService(const QString &service)
{
    // TODO: this watchdog exists to make really safe that we at some point get the service
    // but it's probably no longer needed since we explicitly unregister the service with the deconstructor
    if (service == m_serviceName && QDBusConnection::sessionBus().registerService(m_serviceName) && sender()) {
        sender()->deleteLater(); // bye doggy :'(
        announceService();
    }
}

DBusInterface::~DBusInterface()
{
    QDBusConnection::sessionBus().unregisterService(m_serviceName);
    // KApplication automatically also grabs org.kde.kwin, so it's often been used externally - ensure to free it as well
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.kde.kwin"));
    if (kwinApp()->x11Connection()) {
        xcb_delete_property(kwinApp()->x11Connection(), kwinApp()->x11RootWindow(), atoms->kwin_dbus_service);
    }
}

void DBusInterface::announceService()
{
    if (!kwinApp()->x11Connection()) {
        return;
    }
    const QByteArray service = m_serviceName.toUtf8();
    xcb_change_property(kwinApp()->x11Connection(), XCB_PROP_MODE_REPLACE, kwinApp()->x11RootWindow(), atoms->kwin_dbus_service,
                        atoms->utf8_string, 8, service.size(), service.constData());
}

// wrap void methods with no arguments to Workspace
#define WRAP(name) \
void DBusInterface::name() \
{\
    Workspace::self()->name();\
}

WRAP(reconfigure)

#undef WRAP

void DBusInterface::killWindow()
{
    Workspace::self()->slotKillWindow();
}

void DBusInterface::unclutterDesktop()
{
    win::unclutter_desktop();
}

// wrap returning methods with no arguments to Workspace
#define WRAP( rettype, name ) \
rettype DBusInterface::name( ) \
{\
    return Workspace::self()->name(); \
}

WRAP(QString, supportInformation)

#undef WRAP

bool DBusInterface::startActivity(const QString& /*in0*/)
{
    return false;
}

bool DBusInterface::stopActivity(const QString& /*in0*/)
{
    return false;
}

int DBusInterface::currentDesktop()
{
    return win::virtual_desktop_manager::self()->current();
}

bool DBusInterface::setCurrentDesktop(int desktop)
{
    return win::virtual_desktop_manager::self()->setCurrent(desktop);
}

void DBusInterface::nextDesktop()
{
    win::virtual_desktop_manager::self()->moveTo<win::virtual_desktop_next>();
}

void DBusInterface::previousDesktop()
{
    win::virtual_desktop_manager::self()->moveTo<win::virtual_desktop_previous>();
}

void DBusInterface::showDebugConsole()
{
    auto console = kwinApp()->create_debug_console();
    console->show();
}

void DBusInterface::enableFtrace(bool enable)
{
    const QString name = QStringLiteral("org.kde.kwin.enableFtrace");
#if HAVE_PERF
    if (!Perf::Ftrace::valid()) {
        const QString msg = QStringLiteral("Ftrace marker not available");
        QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
        return;
    }
    if (!Perf::Ftrace::setEnabled(enable)) {
        const QString msg = QStringLiteral("Ftrace marker is available but could not be ").append(
                    enable ? "enabled" : "disabled");
        QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
    }
    return;
#else
    Q_UNUSED(enable)
    const QString msg = QStringLiteral("KWin built without ftrace marking capability");
    QDBusConnection::sessionBus().send(message().createErrorReply(name, msg));
#endif
}

namespace {
QVariantMap clientToVariantMap(Toplevel const* c)
{
    return {
        {QStringLiteral("resourceClass"), c->resourceClass()},
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
            static_cast<int>(c->maximizeMode() & win::maximize_mode::vertical)}
    };
}
}

QVariantMap DBusInterface::queryWindowInfo()
{
    m_replyQueryWindowInfo = message();
    setDelayedReply(true);
    kwinApp()->input->start_interactive_window_selection(
        [this] (Toplevel* t) {
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
            QDBusConnection::sessionBus().send(m_replyQueryWindowInfo.createReply(clientToVariantMap(t)));
        }
    );
    return QVariantMap{};
}

QVariantMap DBusInterface::getWindowInfo(const QString &uuid)
{
    const auto id = QUuid::fromString(uuid);
    const auto client = workspace()->findAbstractClient([&id] (Toplevel const* c) { return c->internalId() == id; });
    if (client) {
        return clientToVariantMap(client);
    } else {
        return {};
    }
}

CompositorDBusInterface::CompositorDBusInterface(render::compositor *parent)
    : QObject(parent)
    , m_compositor(parent)
{
    connect(m_compositor, &render::compositor::compositingToggled,
            this, &CompositorDBusInterface::compositingToggled);
    new CompositingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Compositor"), this);
    dbus.connect(QString(), QStringLiteral("/Compositor"), QStringLiteral("org.kde.kwin.Compositing"),
                 QStringLiteral("reinit"), this, SLOT(reinitialize()));
}

QString CompositorDBusInterface::compositingNotPossibleReason() const
{
    return kwinApp()->get_base().render->compositingNotPossibleReason();
}

QString CompositorDBusInterface::compositingType() const
{
    if (!m_compositor->scene()) {
        return QStringLiteral("none");
    }
    switch (m_compositor->scene()->compositingType()) {
    case XRenderCompositing:
        return QStringLiteral("xrender");
    case OpenGLCompositing:
        if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
            return QStringLiteral("gles");
        } else {
            return QStringLiteral("gl2");
        }
    case QPainterCompositing:
        return QStringLiteral("qpainter");
    case NoCompositing:
    default:
        return QStringLiteral("none");
    }
}

bool CompositorDBusInterface::isActive() const
{
    return m_compositor->isActive();
}

bool CompositorDBusInterface::isCompositingPossible() const
{
    return kwinApp()->get_base().render->compositingPossible();
}

bool CompositorDBusInterface::isOpenGLBroken() const
{
    return kwinApp()->get_base().render->openGLCompositingIsBroken();
}

bool CompositorDBusInterface::platformRequiresCompositing() const
{
    return kwinApp()->get_base().render->requiresCompositing();
}

void CompositorDBusInterface::resume()
{
    using X11Compositor = render::x11::compositor;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        static_cast<X11Compositor*>(m_compositor)->resume(X11Compositor::ScriptSuspend);
    }
}

void CompositorDBusInterface::suspend()
{
    using X11Compositor = render::x11::compositor;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        static_cast<X11Compositor*>(m_compositor)->suspend(X11Compositor::ScriptSuspend);
    }
}

void CompositorDBusInterface::reinitialize()
{
    m_compositor->reinitialize();
}

QStringList CompositorDBusInterface::supportedOpenGLPlatformInterfaces() const
{
    QStringList interfaces;
    bool supportsGlx = false;
#if HAVE_EPOXY_GLX
    supportsGlx = (kwinApp()->operationMode() == Application::OperationModeX11);
#endif
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        supportsGlx = false;
    }
    if (supportsGlx) {
        interfaces << QStringLiteral("glx");
    }
    interfaces << QStringLiteral("egl");
    return interfaces;
}




VirtualDesktopManagerDBusInterface::VirtualDesktopManagerDBusInterface(win::virtual_desktop_manager *parent)
    : QObject(parent)
    , m_manager(parent)
{
    qDBusRegisterMetaType<KWin::DBusDesktopDataStruct>();
    qDBusRegisterMetaType<KWin::DBusDesktopDataVector>();

    new VirtualDesktopManagerAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        this
    );

    connect(m_manager, &win::virtual_desktop_manager::currentChanged, this,
        [this](uint previousDesktop, uint newDesktop) {
            Q_UNUSED(previousDesktop);
            Q_UNUSED(newDesktop);
            Q_EMIT currentChanged(m_manager->currentDesktop()->id());
        }
    );

    connect(m_manager, &win::virtual_desktop_manager::countChanged, this,
        [this](uint previousCount, uint newCount) {
            Q_UNUSED(previousCount);
            Q_EMIT countChanged(newCount);
            Q_EMIT desktopsChanged(desktops());
        }
    );

    connect(m_manager, &win::virtual_desktop_manager::navigationWrappingAroundChanged, this,
        [this]() {
            Q_EMIT navigationWrappingAroundChanged(isNavigationWrappingAround());
        }
    );

    connect(m_manager, &win::virtual_desktop_manager::rowsChanged, this, &VirtualDesktopManagerDBusInterface::rowsChanged);

    for (auto vd : m_manager->desktops()) {
        connect(vd, &win::virtual_desktop::x11DesktopNumberChanged, this,
            [this, vd]() {
                DBusDesktopDataStruct data{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                Q_EMIT desktopDataChanged(vd->id(), data);
                Q_EMIT desktopsChanged(desktops());
            }
        );
        connect(vd, &win::virtual_desktop::nameChanged, this,
            [this, vd]() {
                DBusDesktopDataStruct data{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                Q_EMIT desktopDataChanged(vd->id(), data);
                Q_EMIT desktopsChanged(desktops());
            }
        );
    }
    connect(m_manager, &win::virtual_desktop_manager::desktopCreated, this,
        [this](auto vd) {
            connect(vd, &win::virtual_desktop::x11DesktopNumberChanged, this,
                [this, vd]() {
                    DBusDesktopDataStruct data{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                    Q_EMIT desktopDataChanged(vd->id(), data);
                    Q_EMIT desktopsChanged(desktops());
                }
            );
            connect(vd, &win::virtual_desktop::nameChanged, this,
                [this, vd]() {
                    DBusDesktopDataStruct data{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                    Q_EMIT desktopDataChanged(vd->id(), data);
                    Q_EMIT desktopsChanged(desktops());
                }
            );
            DBusDesktopDataStruct data{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopCreated(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        }
    );
    connect(m_manager, &win::virtual_desktop_manager::desktopRemoved, this,
        [this](auto vd) {
            Q_EMIT desktopRemoved(vd->id());
            Q_EMIT desktopsChanged(desktops());
        }
    );
}

uint VirtualDesktopManagerDBusInterface::count() const
{
    return m_manager->count();
}

void VirtualDesktopManagerDBusInterface::setRows(uint rows)
{
    if (static_cast<uint>(m_manager->grid().height()) == rows) {
        return;
    }

    m_manager->setRows(rows);
    m_manager->save();
}

uint VirtualDesktopManagerDBusInterface::rows() const
{
    return m_manager->rows();
}

void VirtualDesktopManagerDBusInterface::setCurrent(const QString &id)
{
    if (m_manager->currentDesktop()->id() == id) {
        return;
    }

    auto *vd = m_manager->desktopForId(id.toUtf8());
    if (vd) {
        m_manager->setCurrent(vd);
    }
}

QString VirtualDesktopManagerDBusInterface::current() const
{
    return m_manager->currentDesktop()->id();
}

void VirtualDesktopManagerDBusInterface::setNavigationWrappingAround(bool wraps)
{
    if (m_manager->isNavigationWrappingAround() == wraps) {
        return;
    }

    m_manager->setNavigationWrappingAround(wraps);
}

bool VirtualDesktopManagerDBusInterface::isNavigationWrappingAround() const
{
    return m_manager->isNavigationWrappingAround();
}

DBusDesktopDataVector VirtualDesktopManagerDBusInterface::desktops() const
{
    const auto desks = m_manager->desktops();
    DBusDesktopDataVector desktopVect;
    desktopVect.reserve(m_manager->count());

    std::transform(desks.constBegin(), desks.constEnd(),
        std::back_inserter(desktopVect),
        [] (auto vd) {
            return DBusDesktopDataStruct{.position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
        }
    );

    return desktopVect;
}

void VirtualDesktopManagerDBusInterface::createDesktop(uint position, const QString &name)
{
    m_manager->createVirtualDesktop(position, name);
}

void VirtualDesktopManagerDBusInterface::setDesktopName(const QString &id, const QString &name)
{
    auto vd = m_manager->desktopForId(id.toUtf8());
    if (!vd) {
        return;
    }
    if (vd->name() == name) {
        return;
    }

    vd->setName(name);
    m_manager->save();
}

void VirtualDesktopManagerDBusInterface::removeDesktop(const QString &id)
{
    m_manager->removeVirtualDesktop(id.toUtf8());
}

} // namespace
