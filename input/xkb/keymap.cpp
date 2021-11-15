/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keymap.h"

#include "manager.h"

#include <sys/mman.h>
#include <xkbcommon/xkbcommon.h>

namespace KWin::input::xkb
{

keymap::keymap(xkb_keymap* keymap)
    : raw{keymap}
    , cache{xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1)}
{
    xkb_keymap_ref(raw);
}

keymap::keymap(int fd, uint32_t size, xkb_context* context)
{
    auto map = reinterpret_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map == MAP_FAILED) {
        qCDebug(KWIN_XKB) << "Could not map keymap from fd" << fd;
        // Throw specific error
        throw;
    }

    auto keymap = xkb_keymap_new_from_string(
        context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_MAP_COMPILE_PLACEHOLDER);
    munmap(map, size);

    if (!keymap) {
        qCDebug(KWIN_XKB) << "Could not get new keymap string from map.";
        // Throw specific error
        throw;
    }

    raw = keymap;
    cache = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
}

keymap::~keymap()
{
    delete cache;
    if (raw) {
        xkb_keymap_unref(raw);
    }
}

}
