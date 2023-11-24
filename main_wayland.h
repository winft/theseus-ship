/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_WAYLAND_H
#define KWIN_MAIN_WAYLAND_H

#include <base/backend/wlroots/platform.h>
#include <base/wayland/xwl_platform.h>

#include <QApplication>
#include <QProcessEnvironment>
#include <memory>

namespace KWin
{

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

    using base_t = base::backend::wlroots::platform<base::wayland::xwl_platform>;
    std::unique_ptr<base_t> base;

    QProcess* exit_with_process{nullptr};
};

}

#endif
