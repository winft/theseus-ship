/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_X11_H
#define KWIN_MAIN_X11_H
#include "main.h"

#include "base/backend/x11/platform.h"
#include "render/backend/x11/platform.h"

#include <memory>

namespace KWin
{

namespace base::x11
{
template<typename Space>
class xcb_event_filter;
}

namespace win::x11
{
template<typename Base>
class space;
}

class KWinSelectionOwner;

class ApplicationX11 : public Application
{
    Q_OBJECT
public:
    ApplicationX11(int &argc, char **argv);
    ~ApplicationX11() override;

    void start();

    void setReplace(bool replace);
    void notifyKSplash() override;

private Q_SLOTS:
    void lostSelection();

private:
    void crashChecking();
    void setupCrashHandler();

    static void crashHandler(int signal);

    base::backend::x11::platform base;

    QScopedPointer<KWinSelectionOwner> owner;
    std::unique_ptr<base::x11::xcb_event_filter<win::x11::space<base::x11::platform>>> event_filter;
    bool m_replace;
};

}

#endif
