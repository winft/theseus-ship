/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define static
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#undef static
}
