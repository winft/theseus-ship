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
#include "base/x11/xcb/extensions.h"
#include "debug/perf/ftrace.h"
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
#include <unistd.h>

#if __has_include(<malloc.h>)
#include <malloc.h>
#endif

// xcb
#include <xcb/damage.h>
#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
#endif

Q_DECLARE_METATYPE(KSharedConfigPtr)

namespace KWin
{

int Application::crashes = 0;

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    qDebug("Starting KWinFT %s", KWIN_VERSION_STRING);

    if(!Perf::Ftrace::setEnabled(qEnvironmentVariableIsSet("KWIN_PERF_FTRACE"))) {
        qCWarning(KWIN_CORE) << "Can't enable Ftrace via environment variable.";
    }

    qRegisterMetaType<base::options_qobject::WindowOperation>("base::options::WindowOperation");
    qRegisterMetaType<KWin::EffectWindow*>();
    qRegisterMetaType<Wrapland::Server::Surface*>("Wrapland::Server::Surface*");

    // We want all QQuickWindows with an alpha buffer, do here as a later Workspace might create
    // QQuickWindows.
    QQuickWindow::setDefaultAlphaBuffer(true);
}

void Application::prepare_start()
{
    setQuitOnLastWindowClosed(false);
}

Application::~Application() = default;

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

static const QString s_crashesOption = QStringLiteral("crashes");

void Application::setupCommandLine(QCommandLineParser *parser)
{
    QCommandLineOption crashesOption(s_crashesOption, i18n("Indicate that KWin has recently crashed n times"), QStringLiteral("n"));

    parser->setApplicationDescription(i18n("KDE window manager"));
    parser->addOption(crashesOption);
    KAboutData::applicationData().setupCommandLine(parser);
}

void Application::processCommandLine(QCommandLineParser *parser)
{
    KAboutData aboutData = KAboutData::applicationData();
    aboutData.processCommandLine(parser);
    crashes = parser->value(s_crashesOption).toInt();
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
    const int pagesize = sysconf(_SC_PAGESIZE);
    mallopt(M_TRIM_THRESHOLD, 5*pagesize);
#endif // M_TRIM_THRESHOLD
}

void Application::setupLocalizedString()
{
    KLocalizedString::setApplicationDomain("kwin");
}

} // namespace

