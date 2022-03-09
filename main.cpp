/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

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

#include "main.h"
#include <config-kwin.h>

#include "base/logging.h"
#include "base/options.h"
#include "base/seat/session.h"
#include "base/x11/event_filter_manager.h"
#include "base/x11/xcb/extensions.h"
#include "debug/perf/ftrace.h"
#include "desktop/screen_locker_watcher.h"
#include "render/compositor.h"
#include "input/global_shortcuts_manager.h"
#include "input/platform.h"
#include "input/redirect.h"
#include "win/space.h"

#include <kwineffects/effect_window.h>

// KDE
#include <KAboutData>
#include <KLocalizedString>
#include <KSharedConfig>
#include <Wrapland/Server/surface.h>
// Qt
#include <qplatformdefs.h>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>
#include <QX11Info>

#include <cerrno>

// system
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif // HAVE_MALLOC_H

// xcb
#include <xcb/damage.h>
#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
#endif

Q_DECLARE_METATYPE(KSharedConfigPtr)

namespace KWin
{

int Application::crashes = 0;

void Application::setX11ScreenNumber(int screenNumber)
{
    x11_screen_number = screenNumber;
}

int Application::x11ScreenNumber()
{
    return x11_screen_number;
}

Application::Application(Application::OperationMode mode, int &argc, char **argv)
    : QApplication(argc, argv)
    , x11_event_filters{new base::x11::event_filter_manager}
    , m_configLock(false)
    , m_config()
    , m_kxkbConfig()
    , m_inputConfig()
    , m_operationMode(mode)
{
    qDebug("Starting KWinFT %s", KWIN_VERSION_STRING);

#if HAVE_PERF
    if(!Perf::Ftrace::valid(this, true)) {
        qCWarning(KWIN_CORE) << "Not able to setup Ftracing interface.";
    }
#endif

    qRegisterMetaType<base::options::WindowOperation>("base::options::WindowOperation");
    qRegisterMetaType<KWin::EffectWindow*>();
    qRegisterMetaType<Wrapland::Server::Surface*>("Wrapland::Server::Surface*");
    qRegisterMetaType<KSharedConfigPtr>();

    // We want all QQuickWindows with an alpha buffer, do here as a later Workspace might create
    // QQuickWindows.
    QQuickWindow::setDefaultAlphaBuffer(true);
}

void Application::setConfigLock(bool lock)
{
    m_configLock = lock;
}

Application::OperationMode Application::operationMode() const
{
    return m_operationMode;
}

void Application::setOperationMode(OperationMode mode)
{
    m_operationMode = mode;
}

bool Application::shouldUseWaylandForCompositing() const
{
    return m_operationMode == OperationModeWaylandOnly || m_operationMode == OperationModeXwayland;
}

void Application::prepare_start()
{
    setQuitOnLastWindowClosed(false);

    if (!m_config) {
        m_config = KSharedConfig::openConfig();
    }
    if (!m_config->isImmutable() && m_configLock) {
        // TODO: This shouldn't be necessary
        //config->setReadOnly( true );
        m_config->reparseConfiguration();
    }
    if (!m_kxkbConfig) {
        m_kxkbConfig = KSharedConfig::openConfig(QStringLiteral("kxkbrc"), KConfig::NoGlobals);
    }
    if (!m_inputConfig) {
        m_inputConfig = KSharedConfig::openConfig(QStringLiteral("kcminputrc"), KConfig::NoGlobals);
    }

    screen_locker_watcher = std::make_unique<desktop::screen_locker_watcher>();
}

Application::~Application() = default;

void Application::resetCrashesCount()
{
    crashes = 0;
}

void Application::setCrashCount(int count)
{
    crashes = count;
}

bool Application::wasCrash()
{
    return crashes > 0;
}

void Application::createAboutData()
{
    KAboutData aboutData(QStringLiteral(KWIN_NAME),          // The program name used internally
                         i18n("KWinFT"),                       // A displayable program name string
                         QStringLiteral(KWIN_VERSION_STRING), // The program version string
                         i18n("KDE window manager"),          // Short description of what the app does
                         KAboutLicense::GPL,            // The license this code is released under
                         i18n("(c) 1999-2020, The KDE Developers"),   // Copyright Statement
                         QString(),
                         QStringLiteral("kwinft.org"),
                         QStringLiteral("https://gitlab.com/kwinft/kwinft/-/issues"));

    aboutData.addAuthor(i18n("Matthias Ettrich"), QString(), QStringLiteral("ettrich@kde.org"));
    aboutData.addAuthor(i18n("Cristian Tibirna"), QString(), QStringLiteral("tibirna@kde.org"));
    aboutData.addAuthor(i18n("Daniel M. Duley"),  QString(), QStringLiteral("mosfet@kde.org"));
    aboutData.addAuthor(i18n("Luboš Luňák"),      QString(), QStringLiteral("l.lunak@kde.org"));
    aboutData.addAuthor(i18n("Martin Flöser"),    QString(), QStringLiteral("mgraesslin@kde.org"));
    aboutData.addAuthor(i18n("David Edmundson"),  QStringLiteral("Maintainer"), QStringLiteral("davidedmundson@kde.org"));
    aboutData.addAuthor(i18n("Roman Gilg"),       QStringLiteral("Project lead"), QStringLiteral("subdiff@gmail.com"));
    aboutData.addAuthor(i18n("Vlad Zahorodnii"),  QStringLiteral("Maintainer"), QStringLiteral("vlad.zahorodnii@kde.org"));
    KAboutData::setApplicationData(aboutData);
}

static const QString s_lockOption = QStringLiteral("lock");
static const QString s_crashesOption = QStringLiteral("crashes");

void Application::setupCommandLine(QCommandLineParser *parser)
{
    QCommandLineOption lockOption(s_lockOption, i18n("Disable configuration options"));
    QCommandLineOption crashesOption(s_crashesOption, i18n("Indicate that KWin has recently crashed n times"), QStringLiteral("n"));

    parser->setApplicationDescription(i18n("KDE window manager"));
    parser->addOption(lockOption);
    parser->addOption(crashesOption);
    KAboutData::applicationData().setupCommandLine(parser);
}

void Application::processCommandLine(QCommandLineParser *parser)
{
    KAboutData aboutData = KAboutData::applicationData();
    aboutData.processCommandLine(parser);
    setConfigLock(parser->isSet(s_lockOption));
    Application::setCrashCount(parser->value(s_crashesOption).toInt());
}

void Application::setupTranslator()
{
    QTranslator *qtTranslator = new QTranslator(qApp);
    qtTranslator->load("qt_" + QLocale::system().name(),
                       QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    installTranslator(qtTranslator);
}

void Application::setupMalloc()
{
#ifdef M_TRIM_THRESHOLD
    // Prevent fragmentation of the heap by malloc (glibc).
    //
    // The default threshold is 128*1024, which can result in a large memory usage
    // due to fragmentation especially if we use the raster graphicssystem. On the
    // otherside if the threshold is too low, free() starts to permanently ask the kernel
    // about shrinking the heap.
#ifdef HAVE_UNISTD_H
    const int pagesize = sysconf(_SC_PAGESIZE);
#else
    const int pagesize = 4*1024;
#endif // HAVE_UNISTD_H
    mallopt(M_TRIM_THRESHOLD, 5*pagesize);
#endif // M_TRIM_THRESHOLD
}

void Application::setupLocalizedString()
{
    KLocalizedString::setApplicationDomain("kwin");
}

bool Application::is_screen_locked() const
{
    return false;
}

base::wayland::server* Application::get_wayland_server()
{
    return nullptr;
}

void Application::createOptions()
{
    options = std::make_unique<base::options>();
    options->loadConfig();
    options->loadCompositingConfig(false);
}

static uint32_t get_monotonic_time()
{
    timespec ts;

    const int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (result)
        qCWarning(KWIN_CORE, "Failed to query monotonic time: %s", strerror(errno));

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
}

void Application::update_x11_time_from_clock()
{
    switch (m_operationMode) {
    case Application::OperationModeX11:
        setX11Time(QX11Info::getTimestamp(), Application::TimestampUpdate::Always);
        break;

    case Application::OperationModeXwayland:
        setX11Time(get_monotonic_time(), Application::TimestampUpdate::Always);
        break;

    default:
        // Do not update the current X11 time stamp if it's the Wayland only session.
        break;
    }
}

void Application::update_x11_time_from_event(xcb_generic_event_t *event)
{
    xcb_timestamp_t time = XCB_TIME_CURRENT_TIME;
    const uint8_t eventType = event->response_type & ~0x80;
    switch(eventType) {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        time = reinterpret_cast<xcb_key_press_event_t*>(event)->time;
        break;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        time = reinterpret_cast<xcb_button_press_event_t*>(event)->time;
        break;
    case XCB_MOTION_NOTIFY:
        time = reinterpret_cast<xcb_motion_notify_event_t*>(event)->time;
        break;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        time = reinterpret_cast<xcb_enter_notify_event_t*>(event)->time;
        break;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
    case XCB_KEYMAP_NOTIFY:
    case XCB_EXPOSE:
    case XCB_GRAPHICS_EXPOSURE:
    case XCB_NO_EXPOSURE:
    case XCB_VISIBILITY_NOTIFY:
    case XCB_CREATE_NOTIFY:
    case XCB_DESTROY_NOTIFY:
    case XCB_UNMAP_NOTIFY:
    case XCB_MAP_NOTIFY:
    case XCB_MAP_REQUEST:
    case XCB_REPARENT_NOTIFY:
    case XCB_CONFIGURE_NOTIFY:
    case XCB_CONFIGURE_REQUEST:
    case XCB_GRAVITY_NOTIFY:
    case XCB_RESIZE_REQUEST:
    case XCB_CIRCULATE_NOTIFY:
    case XCB_CIRCULATE_REQUEST:
        // no timestamp
        return;
    case XCB_PROPERTY_NOTIFY:
        time = reinterpret_cast<xcb_property_notify_event_t*>(event)->time;
        break;
    case XCB_SELECTION_CLEAR:
        time = reinterpret_cast<xcb_selection_clear_event_t*>(event)->time;
        break;
    case XCB_SELECTION_REQUEST:
        time = reinterpret_cast<xcb_selection_request_event_t*>(event)->time;
        break;
    case XCB_SELECTION_NOTIFY:
        time = reinterpret_cast<xcb_selection_notify_event_t*>(event)->time;
        break;
    case XCB_COLORMAP_NOTIFY:
    case XCB_CLIENT_MESSAGE:
    case XCB_MAPPING_NOTIFY:
    case XCB_GE_GENERIC:
        // no timestamp
        return;
    default:
        // extension handling
        if (base::x11::xcb::extensions::self()) {
            if (eventType == base::x11::xcb::extensions::self()->shape_notify_event()) {
                time = reinterpret_cast<xcb_shape_notify_event_t*>(event)->server_time;
            }
            if (eventType == base::x11::xcb::extensions::self()->damage_notify_event()) {
                time = reinterpret_cast<xcb_damage_notify_event_t*>(event)->timestamp;
            }
        }
        break;
    }
    setX11Time(time);
}

QProcessEnvironment Application::processStartupEnvironment() const
{
    return QProcessEnvironment::systemEnvironment();
}

void Application::setProcessStartupEnvironment(QProcessEnvironment const& /*environment*/)
{
}

} // namespace

