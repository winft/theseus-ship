/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <functional>

class KStartupInfo;

namespace KWin
{

class Toplevel;

namespace win
{

class internal_window;
class shortcut_dialog;
class tabbox;

class KWIN_EXPORT space_qobject : public QObject
{
    Q_OBJECT
public:
    space_qobject(std::function<void()> reconfigure_callback);

public Q_SLOTS:
    void reconfigure();

Q_SIGNALS:
    void desktopPresenceChanged(KWin::Toplevel*, int);
    void currentDesktopChanged(int, KWin::Toplevel*);

    // X11 window
    void clientAdded(KWin::Toplevel*);
    void clientRemoved(KWin::Toplevel*);

    void wayland_window_added(KWin::Toplevel*);
    void wayland_window_removed(KWin::Toplevel*);

    void remnant_created(KWin::Toplevel* remnant);

    void clientActivated(KWin::Toplevel*);
    void clientDemandsAttentionChanged(KWin::Toplevel*, bool);
    void clientMinimizedChanged(KWin::Toplevel*);
    void unmanagedAdded(KWin::Toplevel*);
    void unmanagedRemoved(KWin::Toplevel*);
    void window_deleted(KWin::Toplevel*);
    void configChanged();
    void showingDesktopChanged(bool showing);
    void internalClientAdded(KWin::win::internal_window* client);
    void internalClientRemoved(KWin::win::internal_window* client);
    void surface_id_changed(KWin::Toplevel*, quint32);

private:
    std::function<void()> reconfigure_callback;
};

}

}
