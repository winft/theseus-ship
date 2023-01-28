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

#ifndef MAIN_H
#define MAIN_H

#include "base/platform.h"
#include "input/platform.h"

#include <kwinglobals.h>
#include <config-kwin.h>

#include <QApplication>

#include <memory>

class QCommandLineParser;

namespace KWin
{

namespace base
{

namespace x11
{
class event_filter_manager;
}

}

namespace desktop
{
class screen_locker_watcher;
}

class KWIN_EXPORT Application : public  QApplication
{
    Q_OBJECT
public:
    ~Application() override;

    virtual base::platform& get_base() = 0;

    void setupEventFilters();
    void setupTranslator();
    void setupCommandLine(QCommandLineParser *parser);
    void processCommandLine(QCommandLineParser *parser);

    /**
     * Creates the KAboutData object for the KWin instance and registers it as
     * KAboutData::setApplicationData.
     */
    static void createAboutData();

    static void setupMalloc();
    static void setupLocalizedString();
    virtual void notifyKSplash() {}

    std::unique_ptr<base::x11::event_filter_manager> x11_event_filters;
    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;

Q_SIGNALS:
    void x11ConnectionChanged();
    void x11ConnectionAboutToBeDestroyed();
    void virtualTerminalCreated();

protected:
    Application(int &argc, char **argv);

    void prepare_start();

    static int crashes;
};

inline static Application *kwinApp()
{
    return static_cast<Application*>(QCoreApplication::instance());
}

}

#endif
