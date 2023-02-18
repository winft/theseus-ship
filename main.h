/*
SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

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

class KWIN_EXPORT Application : public  QApplication
{
    Q_OBJECT
public:
    ~Application() override;

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

protected:
    Application(int &argc, char **argv);

    void prepare_start();

    static int crashes;
};

}

#endif
