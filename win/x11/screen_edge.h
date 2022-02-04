/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "atoms.h"
#include "win/screen_edges.h"
#include "xcbutils.h"

namespace KWin::win::x11
{

class screen_edge : public win::screen_edge
{
    Q_OBJECT
public:
    screen_edge(win::screen_edger* edger, Atoms& atoms);
    ~screen_edge() override;

    quint32 window_id() const override;
    /**
     * The approach window is a special window to notice when get close to the screen border but
     * not yet triggering the border.
     */
    quint32 approachWindow() const override;

protected:
    void doGeometryUpdate() override;
    void doActivate() override;
    void doDeactivate() override;
    void doStartApproaching() override;
    void doStopApproaching() override;
    void doUpdateBlocking() override;

private:
    void createWindow();
    void createApproachWindow();

    Xcb::Window m_window{XCB_WINDOW_NONE};
    Xcb::Window m_approachWindow{XCB_WINDOW_NONE};
    QMetaObject::Connection m_cursorPollingConnection;
    Atoms& atoms;
};

inline quint32 screen_edge::window_id() const
{
    return m_window;
}

inline quint32 screen_edge::approachWindow() const
{
    return m_approachWindow;
}

}
