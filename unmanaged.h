/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

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

#ifndef KWIN_UNMANAGED_H
#define KWIN_UNMANAGED_H

#include <netwm.h>

#include "toplevel.h"

namespace KWin
{

class Unmanaged
    : public Toplevel
{
    Q_OBJECT
public:
    explicit Unmanaged();

    // use release()
    ~Unmanaged() override;

    bool windowEvent(xcb_generic_event_t *e);
    bool track(xcb_window_t w);

    bool setupCompositing() override;

public Q_SLOTS:
    void release(ReleaseReason releaseReason = ReleaseReason::Release);
protected:
    void addDamage(const QRegion &damage) override;
private:
    // handlers for X11 events
    void configureNotifyEvent(xcb_configure_notify_event_t *e);
    QWindow *findInternalWindow() const;
};

} // namespace

#endif
