/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/meta.h"

#include "client_machine.h"

#include <KWindowSystem>

#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
QString read_name_property(Win& win, xcb_atom_t atom)
{
    auto const cookie = xcb_icccm_get_text_property_unchecked(connection(), win.xcb_window, atom);
    xcb_icccm_get_text_property_reply_t reply;

    if (xcb_icccm_get_wm_name_reply(connection(), cookie, &reply, nullptr)) {
        QString retVal;
        if (reply.encoding == win.space.atoms->utf8_string) {
            retVal = QString::fromUtf8(QByteArray(reply.name, reply.name_len));
        } else if (reply.encoding == XCB_ATOM_STRING) {
            retVal = QString::fromLocal8Bit(QByteArray(reply.name, reply.name_len));
        }
        xcb_icccm_get_text_property_reply_wipe(&reply);
        return retVal.simplified();
    }

    return QString();
}

template<typename Win>
QString read_name(Win* win)
{
    if (win->info->name() && win->info->name()[0] != '\0') {
        return QString::fromUtf8(win->info->name()).simplified();
    }

    return read_name_property(*win, XCB_ATOM_WM_NAME);
}

// The list is taken from https://www.unicode.org/reports/tr9/ (#154840)
static const QChar LRM(0x200E);

template<typename Win>
void set_caption(Win* win, QString const& _s, bool force = false)
{
    QString s(_s);
    for (int i = 0; i < s.length();) {

        if (!s[i].isPrint()) {

            if (QChar(s[i]).isHighSurrogate() && i + 1 < s.length()
                && QChar(s[i + 1]).isLowSurrogate()) {
                const uint uc = QChar::surrogateToUcs4(s[i], s[i + 1]);

                if (!QChar::isPrint(uc)) {
                    s.remove(i, 2);
                } else {
                    i += 2;
                }
                continue;
            }
            s.remove(i, 1);
            continue;
        }

        ++i;
    }

    auto const changed = (s != win->meta.caption.normal);
    if (!force && !changed) {
        return;
    }

    win->meta.caption.normal = s;

    if (!force && !changed) {
        Q_EMIT win->qobject->captionChanged();
        return;
    }

    auto reset_name = force;
    auto was_suffix = !win->meta.caption.suffix.isEmpty();
    win->meta.caption.suffix.clear();

    QString machine_suffix;
    if (!kwinApp()->options->qobject->condensedTitle()) {
        // machine doesn't qualify for "clean"
        if (win->client_machine->hostname() != client_machine::localhost()
            && !win->client_machine->is_local()) {
            machine_suffix = QLatin1String(" <@")
                + QString::fromUtf8(win->client_machine->hostname()) + QLatin1Char('>') + LRM;
        }
    }
    auto shortcut_suffix = win::shortcut_caption_suffix(win);
    win->meta.caption.suffix = machine_suffix + shortcut_suffix;

    using window_t = typename std::remove_reference_t<decltype(win->space)>::window_t;

    if ((!win::is_special_window(win) || win::is_toolbar(win))
        && win::find_client_with_same_caption(static_cast<window_t*>(win))) {
        int i = 2;

        do {
            win->meta.caption.suffix = machine_suffix + QLatin1String(" <") + QString::number(i)
                + QLatin1Char('>') + LRM;
            i++;
        } while (win::find_client_with_same_caption(static_cast<window_t*>(win)));

        win->info->setVisibleName(win::caption(win).toUtf8().constData());
        reset_name = false;
    }

    if ((was_suffix && win->meta.caption.suffix.isEmpty()) || reset_name) {
        // If it was new window, it may have old value still set, if the window is reused
        win->info->setVisibleName("");
        win->info->setVisibleIconName("");
    } else if (!win->meta.caption.suffix.isEmpty() && !win->iconic_caption.isEmpty()) {
        // Keep the same suffix in iconic name if it's set
        win->info->setVisibleIconName(
            QString(win->iconic_caption + win->meta.caption.suffix).toUtf8().constData());
    }

    Q_EMIT win->qobject->captionChanged();
}

/**
 * Fetches the window's caption (WM_NAME property). It will be
 * stored in the client's caption().
 */
template<typename Win>
void fetch_name(Win* win)
{
    set_caption(win, read_name(win));
}

template<typename Win>
void fetch_iconic_name(Win* win)
{
    QString s;
    if (win->info->iconName() && win->info->iconName()[0] != '\0') {
        s = QString::fromUtf8(win->info->iconName());
    } else {
        s = read_name_property(*win, XCB_ATOM_WM_ICON_NAME);
    }

    if (s == win->iconic_caption) {
        return;
    }

    auto was_set = !win->iconic_caption.isEmpty();
    win->iconic_caption = s;

    if (win->meta.caption.suffix.isEmpty()) {
        return;
    }

    if (!win->iconic_caption.isEmpty()) {
        // Keep the same suffix in iconic name if it's set.
        win->info->setVisibleIconName(QString(s + win->meta.caption.suffix).toUtf8().constData());
    } else if (was_set) {
        win->info->setVisibleIconName("");
    }
}

template<typename Win>
void get_icons(Win* win)
{
    // First read icons from the window itself
    auto const themedIconName = win::icon_from_desktop_file(win);
    if (!themedIconName.isEmpty()) {
        win->control->icon = QIcon::fromTheme(themedIconName);
        Q_EMIT win->qobject->iconChanged();
        return;
    }

    QIcon icon;
    auto readIcon = [win, &icon](int size, bool scale = true) {
        auto const pix = KWindowSystem::icon(win->xcb_window,
                                             size,
                                             size,
                                             scale,
                                             KWindowSystem::NETWM | KWindowSystem::WMHints,
                                             win->info);
        if (!pix.isNull()) {
            icon.addPixmap(pix);
        }
    };

    readIcon(16);
    readIcon(32);
    readIcon(48, false);
    readIcon(64, false);
    readIcon(128, false);

    if (icon.isNull()) {
        // Then try window group
        icon = win->group->icon();
    }

    if (icon.isNull()) {
        for (auto lead : win->transient->leads()) {
            if (!lead->control->icon.isNull()) {
                icon = lead->control->icon;
                break;
            }
        }
    }
    if (icon.isNull()) {
        // And if nothing else, load icon from classhint or xapp icon
        icon.addPixmap(KWindowSystem::icon(win->xcb_window,
                                           32,
                                           32,
                                           true,
                                           KWindowSystem::ClassHint | KWindowSystem::XApp,
                                           win->info));
        icon.addPixmap(KWindowSystem::icon(win->xcb_window,
                                           16,
                                           16,
                                           true,
                                           KWindowSystem::ClassHint | KWindowSystem::XApp,
                                           win->info));
        icon.addPixmap(KWindowSystem::icon(win->xcb_window,
                                           64,
                                           64,
                                           false,
                                           KWindowSystem::ClassHint | KWindowSystem::XApp,
                                           win->info));
        icon.addPixmap(KWindowSystem::icon(win->xcb_window,
                                           128,
                                           128,
                                           false,
                                           KWindowSystem::ClassHint | KWindowSystem::XApp,
                                           win->info));
    }
    win->control->icon = icon;
    Q_EMIT win->qobject->iconChanged();
}

// TODO(romangg): is this still relevant today, i.e. 2020?
//
// Non-transient windows with window role containing '#' are always
// considered belonging to different applications (unless
// the window role is exactly the same). KMainWindow sets
// window role this way by default, and different KMainWindow
// usually "are" different application from user's point of view.
// This help with no-focus-stealing for e.g. konqy reusing.
// On the other hand, if one of the windows is active, they are
// considered belonging to the same application. This is for
// the cases when opening new mainwindow directly from the application,
// e.g. 'Open New Window' in konqy ( active_hack == true ).
template<typename Win>
bool same_app_window_role_match(Win const* c1, Win const* c2, bool active_hack)
{
    if (c1->transient->lead()) {
        while (auto t = dynamic_cast<Win const*>(c1->transient->lead())) {
            c1 = t;
        }
        if (c1->groupTransient()) {
            return c1->group == c2->group;
        }
    }

    if (c2->transient->lead()) {
        while (auto t = dynamic_cast<Win const*>(c2->transient->lead())) {
            c2 = t;
        }
        if (c2->groupTransient()) {
            return c1->group == c2->group;
        }
    }

    int pos1 = c1->windowRole().indexOf('#');
    int pos2 = c2->windowRole().indexOf('#');

    if ((pos1 >= 0 && pos2 >= 0)) {
        if (!active_hack) {
            // without the active hack for focus stealing prevention,
            // different mainwindows are always different apps
            return c1 == c2;
        }
        if (!c1->control->active && !c2->control->active) {
            return c1 == c2;
        }
    }
    return true;
}

template<typename Win>
xcb_window_t get_wm_client_leader(Win& win)
{
    if (win.m_wmClientLeader != XCB_WINDOW_NONE) {
        return win.m_wmClientLeader;
    }
    return win.xcb_window;
}

template<typename Win>
void fetch_wm_client_machine(Win& win)
{
    win.client_machine->resolve(win.xcb_window, get_wm_client_leader(win));
}

template<typename Win>
bool belong_to_same_application(Win const* c1, Win const* c2, win::same_client_check checks)
{
    bool same_app = false;

    // tests that definitely mean they belong together
    if (c1 == c2) {
        same_app = true;
    } else if (c1->transient->lead() && c1->transient->is_follower_of(c2)) {
        // c1 has c2 as mainwindow
        same_app = true;
    } else if (c2->transient->lead() && c2->transient->is_follower_of(c1)) {
        // c2 has c1 as mainwindow
        same_app = true;
    } else if (c1->group == c2->group) {
        // same group
        same_app = true;
    } else if (get_wm_client_leader(*c1) == get_wm_client_leader(*c2)
               && get_wm_client_leader(*c1) != c1->xcb_window
               && get_wm_client_leader(*c2) != c2->xcb_window) {
        // if WM_CLIENT_LEADER is not set, it returns xcb_window,
        // don't use in this test then same client leader
        same_app = true;

        // tests that mean they most probably don't belong together
    } else if ((c1->pid() != c2->pid()
                && !flags(checks & win::same_client_check::allow_cross_process))
               || c1->wmClientMachine(false) != c2->wmClientMachine(false)) {
        // different processes
    } else if (get_wm_client_leader(*c1) != get_wm_client_leader(*c2)
               && get_wm_client_leader(*c1) != c1->xcb_window
               && get_wm_client_leader(*c2) != c2->xcb_window
               && !flags(checks & win::same_client_check::allow_cross_process)) {
        // if WM_CLIENT_LEADER is not set, it returns xcb_window,
        // don't use in this test then
        // different client leader
    } else if (c1->meta.wm_class.res_class != c2->meta.wm_class.res_class) {
        // different apps
    } else if (!same_app_window_role_match(
                   c1, c2, flags(checks & win::same_client_check::relaxed_for_active))
               && !flags(checks & win::same_client_check::allow_cross_process)) {
        // "different" apps
    } else if (c1->pid() == 0 || c2->pid() == 0) {
        // old apps that don't have _NET_WM_PID, consider them different
        // if they weren't found to match above
    } else {
        // looks like it's the same app
        same_app = true;
    }

    return same_app;
}

template<typename Win>
NET::WindowType get_window_type_direct(Win& win)
{
    if (win.remnant) {
        return win.window_type;
    }
    return win.info->windowType(win.supported_default_types);
}

template<typename Win>
NET::WindowType get_window_type(Win& win)
{
    auto wt = get_window_type_direct(win);
    if (!win.control) {
        return wt;
    }

    assert(!win.remnant);

    auto wt2 = win.control->rules.checkType(wt);
    if (wt != wt2) {
        wt = wt2;
        // force hint change
        win.info->setWindowType(wt);
    }

    // hacks here
    if (wt == NET::Unknown) {
        // this is more or less suggested in NETWM spec
        wt = win.transient->lead() ? NET::Dialog : NET::Normal;
    }
    return wt;
}

template<typename Win>
QByteArray get_wm_client_machine(Win& win, bool use_localhost)
{
    assert(win.client_machine);

    if (use_localhost && win.client_machine->is_local()) {
        // Special name for the local machine (localhost).
        return client_machine::localhost();
    }
    return win.client_machine->hostname();
}

template<typename Win>
void fetch_wm_class(Win& win)
{
    set_wm_class(win,
                 QByteArray(win.info->windowClassName()).toLower(),
                 QByteArray(win.info->windowClassClass()).toLower());
}

template<typename Win>
xcb_window_t get_frame_id(Win& win)
{
    if (win.remnant) {
        return win.remnant->data.frame;
    }
    if (!win.control) {
        return win.xcb_window;
    }
    return win.xcb_windows.outer;
}

template<typename Win>
void print_window_debug_info(Win& win, QDebug& stream)
{
    if (win.remnant) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(&win) << "\'";
        return;
    }

    std::string type = "unmanaged";
    std::string caption = "";
    if (win.control) {
        type = "managed";
        caption = win::caption(&win).toStdString();
    }

    stream.nospace();
    stream << "\'x11::window"
           << "(" << QString::fromStdString(type) << "):" << win.xcb_window << ";"
           << ";WMCLASS:" << win.meta.wm_class.res_class << ":" << win.meta.wm_class.res_name
           << ";Caption:" << QString::fromStdString(caption) << "\'";
}

}
