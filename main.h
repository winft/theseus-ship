/*
SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/platform.h"
#include "debug/perf/ftrace.h"
#include "input/platform.h"

#include <config-kwin.h>
#include <kwineffects/effect_window.h>
#include <kwinglobals.h>

#include <KAboutData>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <memory>

#if __has_include(<malloc.h>)
#include <malloc.h>
#endif

namespace KWin
{

inline void app_init()
{
    qDebug("Starting KWinFT %s", KWIN_VERSION_STRING);

    if (!Perf::Ftrace::setEnabled(qEnvironmentVariableIsSet("KWIN_PERF_FTRACE"))) {
        qCWarning(KWIN_CORE) << "Can't enable Ftrace via environment variable.";
    }

    qRegisterMetaType<base::options_qobject::WindowOperation>("base::options::WindowOperation");
    qRegisterMetaType<KWin::EffectWindow*>();
    qRegisterMetaType<Wrapland::Server::Surface*>("Wrapland::Server::Surface*");

    // We want all QQuickWindows with an alpha buffer, do here as a later Workspace might create
    // QQuickWindows.
    QQuickWindow::setDefaultAlphaBuffer(true);
}

inline void app_create_about_data()
{
    KAboutData aboutData(QStringLiteral(KWIN_NAME),           // The program name used internally
                         i18n("KWinFT"),                      // A displayable program name string
                         QStringLiteral(KWIN_VERSION_STRING), // The program version string
                         i18n("KDE window manager"), // Short description of what the app does
                         KAboutLicense::GPL,         // The license this code is released under
                         i18n("(c) 1999-2020, The KDE Developers"), // Copyright Statement
                         QString(),
                         QStringLiteral("kwinft.org"),
                         QStringLiteral("https://gitlab.com/kwinft/kwinft/-/issues"));

    aboutData.addAuthor(i18n("Matthias Ettrich"), QString(), QStringLiteral("ettrich@kde.org"));
    aboutData.addAuthor(i18n("Cristian Tibirna"), QString(), QStringLiteral("tibirna@kde.org"));
    aboutData.addAuthor(i18n("Daniel M. Duley"), QString(), QStringLiteral("mosfet@kde.org"));
    aboutData.addAuthor(i18n("Luboš Luňák"), QString(), QStringLiteral("l.lunak@kde.org"));
    aboutData.addAuthor(i18n("Martin Flöser"), QString(), QStringLiteral("mgraesslin@kde.org"));
    aboutData.addAuthor(
        i18n("David Edmundson"), QString(), QStringLiteral("davidedmundson@kde.org"));
    aboutData.addAuthor(
        i18n("Vlad Zahorodnii"), QString(), QStringLiteral("vlad.zahorodnii@kde.org"));
    aboutData.addAuthor(
        i18n("Roman Gilg"), QStringLiteral("Project lead"), QStringLiteral("subdiff@gmail.com"));
    KAboutData::setApplicationData(aboutData);
}

inline void app_setup_command_line(QCommandLineParser* parser)
{
    QCommandLineOption crashesOption(
        "crashes", i18n("Indicate that KWin has recently crashed n times"), QStringLiteral("n"));

    parser->setApplicationDescription(i18n("KDE window manager"));
    parser->addOption(crashesOption);
    KAboutData::applicationData().setupCommandLine(parser);
}

template<typename App>
void app_process_command_line(App& app, QCommandLineParser* parser)
{
    KAboutData aboutData = KAboutData::applicationData();
    aboutData.processCommandLine(parser);

    if constexpr (requires(App app) { app.crashes; }) {
        app.crashes = parser->value("crashes").toInt();
    }
}

inline void app_setup_malloc()
{
#ifdef M_TRIM_THRESHOLD
    // Prevent fragmentation of the heap by malloc (glibc).
    //
    // The default threshold is 128*1024, which can result in a large memory usage
    // due to fragmentation especially if we use the raster graphicssystem. On the
    // otherside if the threshold is too low, free() starts to permanently ask the kernel
    // about shrinking the heap.
    const int pagesize = sysconf(_SC_PAGESIZE);
    mallopt(M_TRIM_THRESHOLD, 5 * pagesize);
#endif
}

inline void app_setup_localized_string()
{
    KLocalizedString::setApplicationDomain("kwin");
}

}
