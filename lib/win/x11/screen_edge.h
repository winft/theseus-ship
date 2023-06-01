/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/atoms.h"
#include "base/x11/xcb/window.h"
#include "win/screen_edges.h"

namespace KWin::win::x11
{

template<typename Edger>
class screen_edge : public win::screen_edge<Edger>
{
public:
    screen_edge(Edger* edger, base::x11::atoms& atoms)
        : win::screen_edge<Edger>(edger)
        , atoms{atoms}
    {
    }

    quint32 window_id() const override
    {
        return m_window;
    }

    /**
     * The approach window is a special window to notice when get close to the screen border but
     * not yet triggering the border.
     */
    quint32 approachWindow() const override
    {
        return m_approachWindow;
    }

protected:
    void doGeometryUpdate() override
    {
        m_window.set_geometry(this->geometry);
        if (m_approachWindow.is_valid()) {
            m_approachWindow.set_geometry(this->approach_geometry);
        }
    }

    void doActivate() override
    {
        createWindow();
        createApproachWindow();
        doUpdateBlocking();
    }

    void doDeactivate() override
    {
        m_window.reset();
        m_approachWindow.reset();
    }

    void doStartApproaching() override
    {
        if (!this->activatesForPointer()) {
            return;
        }
        m_approachWindow.unmap();

        auto cursor = this->edger->space.input->cursor.get();
        using cursor_t = std::remove_pointer_t<decltype(cursor)>;
        m_cursorPollingConnection = QObject::connect(
            cursor, &cursor_t::pos_changed, this->qobject.get(), [this](auto const& pos) {
                this->updateApproaching(pos);
            });
    }

    void doStopApproaching() override
    {
        if (!m_cursorPollingConnection) {
            return;
        }
        QObject::disconnect(m_cursorPollingConnection);
        m_cursorPollingConnection = QMetaObject::Connection();
        m_approachWindow.map();
    }

    void doUpdateBlocking() override
    {
        if (this->reserved_count == 0) {
            return;
        }
        if (this->is_blocked) {
            m_window.unmap();
            m_approachWindow.unmap();
        } else {
            m_window.map();
            m_approachWindow.map();
        }
    }

private:
    void createWindow()
    {
        if (m_window.is_valid()) {
            return;
        }

        auto const& x11_data = this->edger->space.base.x11_data;
        const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        const uint32_t values[] = {true,
                                   XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        m_window.create(x11_data.connection,
                        x11_data.root_window,
                        this->geometry,
                        XCB_WINDOW_CLASS_INPUT_ONLY,
                        mask,
                        values);
        m_window.map();
        // Set XdndAware on the windows, so that DND enter events are received (#86998)
        xcb_atom_t version = 4; // XDND version
        xcb_change_property(x11_data.connection,
                            XCB_PROP_MODE_REPLACE,
                            m_window,
                            atoms.xdnd_aware,
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            reinterpret_cast<unsigned char*>(&version));
    }

    void createApproachWindow()
    {
        if (!this->activatesForPointer()) {
            return;
        }
        if (m_approachWindow.is_valid()) {
            return;
        }
        if (!this->approach_geometry.isValid()) {
            return;
        }

        auto const& x11_data = this->edger->space.base.x11_data;
        const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        const uint32_t values[] = {true,
                                   XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        m_approachWindow.create(x11_data.connection,
                                x11_data.root_window,
                                this->approach_geometry,
                                XCB_WINDOW_CLASS_INPUT_ONLY,
                                mask,
                                values);
        m_approachWindow.map();
    }

    base::x11::xcb::window m_window;
    base::x11::xcb::window m_approachWindow;
    QMetaObject::Connection m_cursorPollingConnection;
    base::x11::atoms& atoms;
};

}
