/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_X11_H
#define KWIN_MAIN_X11_H
#include "main.h"

#include "base/platform.h"
#include "base/backend/x11.h"
#include "render/backend/x11/x11_platform.h"

#include <memory>

namespace KWin
{

class KWinSelectionOwner;
class Workspace;

class ApplicationX11 : public Application
{
    Q_OBJECT
public:
    ApplicationX11(int &argc, char **argv);
    ~ApplicationX11() override;

    debug::console* create_debug_console() override;

    void start();

    void setReplace(bool replace);
    void notifyKSplash() override;

protected:
    bool notify(QObject *o, QEvent *e) override;

private Q_SLOTS:
    void lostSelection();

private:
    void crashChecking();
    void setupCrashHandler();

    static void crashHandler(int signal);

    base::platform<base::backend::x11, AbstractOutput> base;
    std::unique_ptr<render::backend::x11::X11StandalonePlatform> render;
    std::unique_ptr<Workspace> workspace;

    QScopedPointer<KWinSelectionOwner> owner;
    bool m_replace;
};

}

#endif
