/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "remnant.h"

#include "rules/rules.h"

#include <KDesktopFile>
#include <klocalizedstring.h>

#include <QDir>
#include <QLatin1String>

namespace KWin::win
{

template<typename Win>
QString caption(Win* win)
{
    if (auto remnant = win->remnant()) {
        return remnant->caption;
    }
    QString cap = win->captionNormal() + win->captionSuffix();
    if (win->control()->unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

template<typename Win>
QString shortcut_caption_suffix(Win* win)
{
    if (win->control()->shortcut().isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + win->control()->shortcut().toString() + QLatin1Char('}');
}

template<typename Win>
void set_desktop_file_name(Win* win, QByteArray name)
{
    name = win->control()->rules().checkDesktopFile(name).toUtf8();
    if (name == win->control()->desktop_file_name()) {
        return;
    }
    win->control()->set_desktop_file_name(name);
    win->updateWindowRules(Rules::DesktopFile);
    Q_EMIT win->desktopFileNameChanged();
}

template<typename Win>
QString icon_from_desktop_file(Win* win)
{
    auto const desktopFileName = QString::fromUtf8(win->control()->desktop_file_name());
    QString desktopFilePath;

    if (QDir::isAbsolutePath(desktopFileName)) {
        desktopFilePath = desktopFileName;
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath
            = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopFileName);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName + QLatin1String(".desktop"));
    }

    KDesktopFile df(desktopFilePath);
    return df.readIcon();
}

}
