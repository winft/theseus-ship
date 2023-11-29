/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_WAYLAND_H
#define KWIN_MAIN_WAYLAND_H

#include <base/wayland/platform.h>
#include <base/wayland/xwl_platform.h>
#include <desktop/platform.h>
#include <script/platform.h>

#include <QApplication>
#include <QProcessEnvironment>
#include <memory>

namespace KWin
{

struct space_mod {
    std::unique_ptr<desktop::platform> desktop;
};

struct base_mod {
    using platform_t = base::wayland::xwl_platform<base_mod>;
    using render_t = render::wayland::xwl_platform<platform_t>;
    using input_t = input::wayland::platform<platform_t>;
    using space_t = win::wayland::xwl_space<platform_t, space_mod>;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<xwl::xwayland<space_t>> xwayland;
    std::unique_ptr<scripting::platform<space_t>> script;
};

class ApplicationWayland : public QApplication
{
    Q_OBJECT
public:
    ApplicationWayland(int &argc, char **argv);
    ~ApplicationWayland() override;

    void start(base::operation_mode mode,
               std::string const& socket_name,
               base::wayland::start_options flags,
               QProcessEnvironment environment);

    void setApplicationsToStart(const QStringList &applications) {
        m_applicationsToStart = applications;
    }
    void setSessionArgument(const QString &session) {
        m_sessionArgument = session;
    }

private:
    void create_xwayland();
    void startSession();

    QStringList m_applicationsToStart;
    QString m_sessionArgument;

    using base_t = base::wayland::xwl_platform<base_mod>;
    std::unique_ptr<base_t> base;

    QProcess* exit_with_process{nullptr};
};

}

#endif
