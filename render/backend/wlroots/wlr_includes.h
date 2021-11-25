/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define static
#if HAVE_WLR_OUTPUT_INIT_RENDER
#include <wlr/render/allocator.h>
#endif
#include <wlr/render/wlr_renderer.h>
#undef static
}
