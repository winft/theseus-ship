/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <cstdint>

struct xkb_context;
struct xkb_keymap;

namespace KWin::input::xkb
{

class KWIN_EXPORT keymap
{
public:
    keymap(xkb_keymap* keymap);
    keymap(int fd, uint32_t size, xkb_context* context);
    ~keymap();

    xkb_keymap* raw{nullptr};
    char* cache{nullptr};
};

}
