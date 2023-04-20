/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/data.h"
#include "kwin_export.h"

#include <QVector>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

class extension_data
{
public:
    extension_data();
    int version;
    int eventBase;
    int errorBase;
    int majorOpcode;
    bool present;
    QByteArray name;
    QVector<QByteArray> opCodes;
    QVector<QByteArray> errorCodes;
};

class KWIN_EXPORT extensions
{
public:
    bool is_shape_available() const
    {
        return m_shape.version > 0;
    }
    bool is_shape_input_available() const;
    int shape_notify_event() const;
    bool has_shape(xcb_window_t w) const;

    bool is_randr_available() const
    {
        return m_randr.present;
    }
    int randr_notify_event() const;

    bool is_damage_available() const
    {
        return m_damage.present;
    }
    int damage_notify_event() const;

    bool is_composite_available() const
    {
        return m_composite.version > 0;
    }
    bool is_composite_overlay_available() const;
    bool is_render_available() const
    {
        return m_render.version > 0;
    }

    bool is_fixes_available() const
    {
        return m_fixes.version > 0;
    }
    int fixes_cursor_notify_event() const;
    bool is_fixes_region_available() const;

    bool is_sync_available() const
    {
        return m_sync.present;
    }
    int sync_alarm_notify_event() const;

    QVector<extension_data> get_data() const;

    bool has_glx() const
    {
        return m_glx.present;
    }
    int glx_event_base() const
    {
        return m_glx.eventBase;
    }
    int glx_major_opcode() const
    {
        return m_glx.majorOpcode;
    }
    int xkb_event_base() const
    {
        return m_xkb.eventBase;
    }

    static extensions* create(x11::data const& data);
    static extensions* self();
    static void destroy();

private:
    extensions(x11::data const& data);
    ~extensions();

    void init();

    template<typename reply, typename T, typename F>
    void init_version(T cookie, F f, extension_data* dataToFill);

    void query_reply(xcb_query_extension_reply_t const* extension, extension_data* dataToFill);

    extension_data m_shape;
    extension_data m_randr;
    extension_data m_damage;
    extension_data m_composite;
    extension_data m_render;
    extension_data m_fixes;
    extension_data m_sync;
    extension_data m_glx;
    extension_data m_xkb;

    x11::data const& data;

    static extensions* s_self;
};

}
