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

class ApplicationWayland : public Application
{
    Q_OBJECT
public:
    using wayland_space = win::wayland::space<base::wayland::platform>;

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
    void handle_server_addons_created();
    void create_xwayland();
    void startSession();

    QStringList m_applicationsToStart;
    QString m_sessionArgument;

    std::unique_ptr<base::backend::wlroots::platform> base;

    QProcess* exit_with_process{nullptr};
};

}

#endif
