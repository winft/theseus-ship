/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_MAIN_WAYLAND_H
#define KWIN_MAIN_WAYLAND_H

#include "main.h"

#include "base/backend/wlroots/platform.h"

#include <QProcessEnvironment>
#include <memory>

namespace KWin
{

namespace input::dbus
{
template<typename Platform>
class tablet_mode_manager;
}

namespace xwl
{
template<typename Space>
class xwayland;
}

class ApplicationWayland : public Application
{
    Q_OBJECT
public:
    using wayland_space = win::wayland::space<base::wayland::platform>;

    std::unique_ptr<base::wayland::server> server;

    ApplicationWayland(int &argc, char **argv);
    ~ApplicationWayland() override;

    bool is_screen_locked() const override;

    base::platform& get_base() override;
    base::wayland::server* get_wayland_server() override;
    debug::console* create_debug_console() override;

    void start();

    void setStartXwayland(bool start) {
        m_startXWayland = start;
    }
    void setApplicationsToStart(const QStringList &applications) {
        m_applicationsToStart = applications;
    }
    void setProcessStartupEnvironment(const QProcessEnvironment &environment) override {
        m_environment = environment;
    }
    void setSessionArgument(const QString &session) {
        m_sessionArgument = session;
    }

    QProcessEnvironment processStartupEnvironment() const override {
        return m_environment;
    }

private:
    void handle_server_addons_created();
    void create_xwayland();
    void startSession();

    bool m_startXWayland = false;
    QStringList m_applicationsToStart;
    QProcessEnvironment m_environment;
    QString m_sessionArgument;

    std::unique_ptr<base::backend::wlroots::platform> base;
    std::unique_ptr<xwl::xwayland<wayland_space>> xwayland;

    std::unique_ptr<input::dbus::tablet_mode_manager<input::wayland::platform>> tablet_mode_manager;

    QProcess* exit_with_process{nullptr};
};

}

#endif
