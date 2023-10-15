/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include <QObject>

namespace KWin::win
{

namespace extended_cursor
{
/**
 * Extension of Qt::CursorShape with values not currently present there
 */
enum Shape {
    SizeNorthWest = 0x100 + 0,
    SizeNorth = 0x100 + 1,
    SizeNorthEast = 0x100 + 2,
    SizeEast = 0x100 + 3,
    SizeWest = 0x100 + 4,
    SizeSouthEast = 0x100 + 5,
    SizeSouth = 0x100 + 6,
    SizeSouthWest = 0x100 + 7
};
}

/**
 * @brief Wrapper round Qt::CursorShape with extensions enums into a single entity
 */
class KWIN_EXPORT cursor_shape
{
public:
    cursor_shape() = default;
    cursor_shape(Qt::CursorShape qtShape);
    cursor_shape(extended_cursor::Shape kwinShape);

    bool operator==(cursor_shape const& o) const;
    operator int() const;

    /**
     * @brief The name of a cursor shape in the theme.
     */
    std::string name() const;

private:
    int m_shape{Qt::ArrowCursor};
};

inline std::vector<std::string> cursor_shape_get_alternative_names(std::string const& name)
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

Q_DECLARE_METATYPE(KWin::win::cursor_shape)
