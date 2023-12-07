/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_X11_H
#define KWIN_MAIN_X11_H
#include "main.h"

#include "base/x11/platform.h"
#include "render/backend/x11/platform.h"
#include <desktop/platform.h>
#include <script/platform.h>

#include <QApplication>
#include <memory>

namespace KWin
{

struct space_mod {
    std::unique_ptr<desktop::platform> desktop;
};

struct base_mod {
    using platform_t = base::x11::platform<base_mod>;
    using render_t = render::x11::platform<platform_t>;
    using input_t = input::x11::platform<platform_t>;
    using space_t = win::x11::space<platform_t, space_mod>;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<scripting::platform<space_t>> script;
};

class ApplicationX11 : public QApplication
{
    Q_OBJECT
public:
    ApplicationX11(int& argc, char** argv);
    ~ApplicationX11() override;

    void start();

    void setReplace(bool replace);
    void notifyKSplash();

    static int crashes;

private Q_SLOTS:
    void lostSelection();

private:
    void crashChecking();

    static void crashHandler(int signal);

    using base_t = base::x11::platform<base_mod>;
    base_t base;
    bool m_replace;
};

}

#endif
