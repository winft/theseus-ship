/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_X11_H
#define KWIN_MAIN_X11_H
#include "main.h"

#include "render/backend/x11/x11_platform.h"

#include <memory>

namespace KWin
{

class KWinSelectionOwner;

class ApplicationX11 : public Application
{
    Q_OBJECT
public:
    ApplicationX11(int &argc, char **argv);
    ~ApplicationX11() override;

    void setReplace(bool replace);
    void notifyKSplash() override;

protected:
    void performStartup() override;
    bool notify(QObject *o, QEvent *e) override;

private Q_SLOTS:
    void lostSelection();

private:
    void crashChecking();
    void setupCrashHandler();

    static void crashHandler(int signal);

    std::unique_ptr<render::backend::x11::X11StandalonePlatform> render;
    QScopedPointer<KWinSelectionOwner> owner;
    bool m_replace;
};

}

#endif
