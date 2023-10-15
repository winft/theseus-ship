/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"

#include "platform.h"
#include "pointer_redirect.h"
#include "singleton_interface.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDBusConnection>
#include <QHash>
#include <xcb/xcb_cursor.h>

namespace KWin::input
{

cursor::cursor(base::x11::data const& x11_data, KSharedConfigPtr config)
    : m_cursorTrackingCounter(0)
    , m_themeName("default")
    , m_themeSize(24)
    , x11_data{x11_data}
    , config{config}
{
    singleton_interface::cursor = this;
    load_theme_settings();
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KGlobalSettings"),
                                          QStringLiteral("org.kde.KGlobalSettings"),
                                          QStringLiteral("notifyChange"),
                                          this,
                                          SLOT(kglobal_settings_notify_change(int, int)));
}

cursor::~cursor()
{
    singleton_interface::cursor = nullptr;
}

void cursor::load_theme_settings()
{
    QString themeName = QString::fromUtf8(qgetenv("XCURSOR_THEME"));
    bool ok = false;

    // XCURSOR_SIZE might not be set (e.g. by startkde)
    uint const themeSize = qEnvironmentVariableIntValue("XCURSOR_SIZE", &ok);

    if (!themeName.isEmpty() && ok) {
        update_theme(themeName, themeSize);
        return;
    }

    // didn't get from environment variables, read from config file
    load_theme_from_kconfig();
}

void cursor::load_theme_from_kconfig()
{
    KConfigGroup mousecfg(config, "Mouse");

    auto const themeName = mousecfg.readEntry("cursorTheme", "default");
    uint const themeSize = mousecfg.readEntry("cursorSize", 24);
    update_theme(themeName, themeSize);
}

void cursor::update_theme(QString const& name, int size)
{
    if (m_themeName != name || m_themeSize != size) {
        m_themeName = name;
        m_themeSize = size;
        xcb_cursors.clear();
        Q_EMIT theme_changed();
    }
}

void cursor::kglobal_settings_notify_change(int type, int arg)
{
    Q_UNUSED(arg)
    if (type == 5 /*CursorChanged*/) {
        config->reparseConfiguration();
        load_theme_from_kconfig();

        // sync to environment
        qputenv("XCURSOR_THEME", m_themeName.toUtf8());
        qputenv("XCURSOR_SIZE", QByteArray::number(m_themeSize));
    }
}

QString const& cursor::theme_name() const
{
    return m_themeName;
}

int cursor::theme_size() const
{
    return m_themeSize;
}

QImage cursor::image() const
{
    return QImage();
}

QPoint cursor::hotspot() const
{
    return QPoint();
}

void cursor::mark_as_rendered()
{
}

bool cursor::is_hidden() const
{
    return hide_count > 0;
}

void cursor::show()
{
    hide_count--;
    if (hide_count == 0) {
        do_show();
    }
}

void cursor::hide()
{
    hide_count++;
    if (hide_count == 1) {
        do_hide();
    }
}

void cursor::do_show()
{
}

void cursor::do_hide()
{
}

QPoint const& cursor::current_pos() const
{
    return m_pos;
}

QPoint cursor::pos()
{
    do_get_pos();
    return m_pos;
}

void cursor::set_pos(QPoint const& pos)
{
    // first query the current pos to not warp to the already existing pos
    if (pos == cursor::pos()) {
        return;
    }
    m_pos = pos;
    do_set_pos();
}

void cursor::set_pos(int x, int y)
{
    cursor::set_pos(QPoint(x, y));
}

xcb_cursor_t cursor::x11_cursor(win::cursor_shape shape)
{
    return x11_cursor(shape.name());
}

xcb_cursor_t cursor::x11_cursor(std::string const& name)
{
    assert(x11_data.connection);

    auto it = xcb_cursors.find(name);
    if (it != xcb_cursors.end()) {
        return it->second;
    }

    if (name.empty()) {
        return XCB_CURSOR_NONE;
    }

    xcb_cursor_context_t* ctx;
    if (xcb_cursor_context_new(x11_data.connection, base::x11::get_default_screen(x11_data), &ctx)
        < 0) {
        return XCB_CURSOR_NONE;
    }

    xcb_cursor_t cursor = xcb_cursor_load_cursor(ctx, name.c_str());
    if (cursor == XCB_CURSOR_NONE) {
        const auto& names = cursor::alternative_names(name);
        for (auto const& cursorName : names) {
            cursor = xcb_cursor_load_cursor(ctx, cursorName.c_str());
            if (cursor != XCB_CURSOR_NONE) {
                break;
            }
        }
    }
    if (cursor != XCB_CURSOR_NONE) {
        xcb_cursors.insert({name, cursor});
    }

    xcb_cursor_context_free(ctx);
    return cursor;
}

void cursor::do_set_pos()
{
    Q_EMIT pos_changed(m_pos);
}

void cursor::do_get_pos()
{
}

void cursor::update_pos(QPoint const& pos)
{
    if (m_pos == pos) {
        return;
    }
    m_pos = pos;
    Q_EMIT pos_changed(m_pos);
}

void cursor::update_pos(int x, int y)
{
    update_pos(QPoint(x, y));
}

bool cursor::is_image_tracking() const
{
    return m_cursorTrackingCounter > 0;
}

void cursor::start_image_tracking()
{
    ++m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 1) {
        do_start_image_tracking();
    }
}

void cursor::stop_image_tracking()
{
    assert(m_cursorTrackingCounter > 0);

    --m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 0) {
        do_stop_image_tracking();
    }
}

void cursor::do_start_image_tracking()
{
}

void cursor::do_stop_image_tracking()
{
}

std::vector<std::string> cursor::alternative_names(std::string const& name) const
{
    static std::unordered_map<std::string, std::vector<std::string>> const alternatives = {
        {
            "left_ptr",
            {
                "arrow",
                "dnd-none",
                "op_left_arrow",
            },
        },
        {
            "cross",
            {
                "crosshair",
                "diamond-cross",
                "cross-reverse",
            },
        },
        {
            "up_arrow",
            {
                "center_ptr",
                "sb_up_arrow",
                "centre_ptr",
            },
        },
        {
            "wait",
            {
                "watch",
                "progress",
            },
        },
        {
            "ibeam",
            {
                "xterm",
                "text",
            },
        },
        {
            "size_all",
            {
                "fleur",
            },
        },
        {
            "pointing_hand",
            {
                "hand2",
                "hand",
                "hand1",
                "pointer",
                "e29285e634086352946a0e7090d73106",
                "9d800788f1b08800ae810202380a0822",
            },
        },
        {
            "size_ver",
            {
                "00008160000006810000408080010102",
                "sb_v_double_arrow",
                "v_double_arrow",
                "n-resize",
                "s-resize",
                "col-resize",
                "top_side",
                "bottom_side",
                "base_arrow_up",
                "base_arrow_down",
                "based_arrow_down",
                "based_arrow_up",
            },
        },
        {
            "size_hor",
            {
                "028006030e0e7ebffc7f7070c0600140",
                "sb_h_double_arrow",
                "h_double_arrow",
                "e-resize",
                "w-resize",
                "row-resize",
                "right_side",
                "left_side",
            },
        },
        {
            "size_bdiag",
            {
                "fcf1c3c7cd4491d801f1e1c78f100000",
                "fd_double_arrow",
                "bottom_left_corner",
                "top_right_corner",
            },
        },
        {
            "size_fdiag",
            {
                "c7088f0f3e6c8088236ef8e1e3e70000",
                "bd_double_arrow",
                "bottom_right_corner",
                "top_left_corner",
            },
        },
        {
            "whats_this",
            {
                "d9ce0ab605698f320427677b458ad60b",
                "left_ptr_help",
                "help",
                "question_arrow",
                "dnd-ask",
                "5c6cd98b3f3ebcb1f9c7f1c204630408",
            },
        },
        {
            "split_h",
            {
                "14fef782d02440884392942c11205230",
                "size_hor",
            },
        },
        {
            "split_v",
            {
                "2870a09082c103050810ffdffffe0204",
                "size_ver",
            },
        },
        {
            "forbidden",
            {
                "03b6e0fcb3499374a867c041f52298f0",
                "circle",
                "dnd-no-drop",
                "not-allowed",
            },
        },
        {
            "left_ptr_watch",
            {
                "3ecb610c1bf2410f44200f48c40d3599",
                "00000000000000020006000e7e9ffc3f",
                "08e8e1c95fe2fc01f976f1e063a24ccd",
            },
        },
        {
            "openhand",
            {
                "9141b49c8149039304290b508d208c40",
                "all_scroll",
                "all-scroll",
            },
        },
        {
            "closedhand",
            {
                "05e88622050804100c20044008402080",
                "4498f0e0c1937ffe01fd06f973665830",
                "9081237383d90e509aa00f00170e968f",
                "fcf21c00b30f7e3f83fe0dfd12e71cff",
            },
        },
        {
            "dnd-link",
            {
                "link",
                "alias",
                "3085a0e285430894940527032f8b26df",
                "640fb0e74195791501fd1ed57b41487f",
                "a2a266d0498c3104214a47bd64ab0fc8",
            },
        },
        {
            "dnd-copy",
            {
                "copy",
                "1081e37283d90000800003c07f3ef6bf",
                "6407b0e94181790501fd1e167b474872",
                "b66166c04f8c3109214a4fbd64a50fc8",
            },
        },
        {
            "dnd-move",
            {
                "move",
            },
        },
        {
            "sw-resize",
            {
                "size_bdiag",
                "fcf1c3c7cd4491d801f1e1c78f100000",
                "fd_double_arrow",
                "bottom_left_corner",
            },
        },
        {
            "se-resize",
            {
                "size_fdiag",
                "c7088f0f3e6c8088236ef8e1e3e70000",
                "bd_double_arrow",
                "bottom_right_corner",
            },
        },
        {
            "ne-resize",
            {
                "size_bdiag",
                "fcf1c3c7cd4491d801f1e1c78f100000",
                "fd_double_arrow",
                "top_right_corner",
            },
        },
        {
            "nw-resize",
            {
                "size_fdiag",
                "c7088f0f3e6c8088236ef8e1e3e70000",
                "bd_double_arrow",
                "top_left_corner",
            },
        },
        {
            "n-resize",
            {
                "size_ver",
                "00008160000006810000408080010102",
                "sb_v_double_arrow",
                "v_double_arrow",
                "col-resize",
                "top_side",
            },
        },
        {
            "e-resize",
            {
                "size_hor",
                "028006030e0e7ebffc7f7070c0600140",
                "sb_h_double_arrow",
                "h_double_arrow",
                "row-resize",
                "left_side",
            },
        },
        {
            "s-resize",
            {
                "size_ver",
                "00008160000006810000408080010102",
                "sb_v_double_arrow",
                "v_double_arrow",
                "col-resize",
                "bottom_side",
            },
        },
        {
            "w-resize",
            {
                "size_hor",
                "028006030e0e7ebffc7f7070c0600140",
                "sb_h_double_arrow",
                "h_double_arrow",
                "right_side",
            },
        },
    };

    auto it = alternatives.find(name);
    if (it != alternatives.end()) {
        return it->second;
    }

    return {};
}

}
