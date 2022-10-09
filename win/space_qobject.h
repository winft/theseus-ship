/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <functional>

namespace KWin::win
{

class KWIN_EXPORT space_qobject : public QObject
{
    Q_OBJECT
public:
    space_qobject(std::function<void()> reconfigure_callback);

public Q_SLOTS:
    void reconfigure();

Q_SIGNALS:
    void desktopPresenceChanged(quint32, int);
    void currentDesktopChanged(int);

    // X11 window
    void clientAdded(quint32);

    // TODO(romangg): This is right now also emitted for Wayland windows with control.
    void clientRemoved(quint32);

    void wayland_window_added(quint32);
    void wayland_window_removed(quint32);

    void remnant_created(quint32 remnant);

    void clientActivated();
    void clientDemandsAttentionChanged(quint32, bool);
    void clientMinimizedChanged(quint32);
    void unmanagedAdded(quint32);
    void unmanagedRemoved(quint32);
    void window_deleted(quint32);
    void configChanged();
    void showingDesktopChanged(bool showing);
    void internalClientAdded(quint32 client);
    void internalClientRemoved(quint32 client);
    void surface_id_changed(quint32, quint32);

private:
    std::function<void()> reconfigure_callback;
};

}
