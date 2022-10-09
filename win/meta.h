/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net.h"
#include "rules/types.h"

#include "utils/algorithm.h"

#include <KDesktopFile>
#include <QDir>
#include <QLatin1String>
#include <klocalizedstring.h>
#include <optional>

namespace KWin::win
{

template<typename Win>
QString caption(Win const* win)
{
    if (win->remnant) {
        return win->remnant->data.caption;
    }
    QString cap = win->meta.caption.normal + win->meta.caption.suffix;
    if (win->control && win->control->unresponsive) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

template<typename Win>
QString shortcut_caption_suffix(Win const* win)
{
    if (win->control->shortcut.isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + win->control->shortcut.toString() + QLatin1Char('}');
}

template<typename Win>
void set_desktop_file_name(Win* win, QByteArray name)
{
    name = win->control->rules.checkDesktopFile(name).toUtf8();
    if (name == win->control->desktop_file_name) {
        return;
    }
    win->control->desktop_file_name = name;
    win->updateWindowRules(rules::type::desktop_file);
    Q_EMIT win->qobject->desktopFileNameChanged();
}

inline QString icon_from_desktop_file(QString const& file_name)
{
    QString desktopFilePath;

    if (QDir::isAbsolutePath(file_name)) {
        desktopFilePath = file_name;
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, file_name);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 file_name + QLatin1String(".desktop"));
    }

    KDesktopFile df(desktopFilePath);
    return df.readIcon();
}

template<typename Win>
QString icon_from_desktop_file(Win* win)
{
    return icon_from_desktop_file(QString::fromUtf8(win->control->desktop_file_name));
}

/**
 * Tells if @p win is "special", in contrast normal windows are with a border, can be moved by the
 * user, can be closed, etc.
 */
template<typename Win>
bool is_special_window(Win const* win)
{
    return is_desktop(win) || is_dock(win) || is_splash(win) || is_toolbar(win)
        || is_notification(win) || is_critical_notification(win) || is_on_screen_display(win);
}

/**
 * Looks for another window with same captionNormal and captionSuffix.
 * If no such window exists @c nullptr is returned.
 */
template<typename Win>
std::optional<typename Win::space_t::window_t> find_client_with_same_caption(Win const* win)
{
    for (auto candidate : win->space.windows) {
        if (std::visit(overload{[&](auto&& candidate) {
                           if constexpr (std::is_same_v<std::decay_t<decltype(candidate)>, Win*>) {
                               if (candidate == win) {
                                   return false;
                               }
                           }
                           if (!candidate->control) {
                               return false;
                           }
                           if (is_special_window(candidate) && !is_toolbar(candidate)) {
                               return false;
                           }
                           if (candidate->meta.caption.normal != win->meta.caption.normal
                               || candidate->meta.caption.suffix != win->meta.caption.suffix) {
                               return false;
                           }
                           return true;
                       }},
                       candidate)) {
            return candidate;
        }
    }
    return {};
}

template<typename Win>
void set_wm_class(Win& win, QByteArray const& res_name, QByteArray const& res_class)
{
    win.meta.wm_class.res_name = res_name;
    win.meta.wm_class.res_class = res_class;
    Q_EMIT win.qobject->windowClassChanged();
}

}
