/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include <cassert>

namespace KWin::render::backend::wlroots
{

struct wlr_non_owning_data_buffer {
    wlr_buffer base;
    void* data;
    uint32_t format;
    size_t stride;
};

static void wlr_non_owning_data_buffer_destroy(wlr_buffer* wlr_buf)
{
    wlr_non_owning_data_buffer* buffer = wl_container_of(wlr_buf, buffer, base);
    free(buffer);
}

static bool wlr_non_owning_data_buffer_begin_data_ptr_access(wlr_buffer* wlr_buf,
                                                             uint32_t /*flags*/,
                                                             void** data,
                                                             uint32_t* format,
                                                             size_t* stride)
{
    wlr_non_owning_data_buffer* buffer = wl_container_of(wlr_buf, buffer, base);
    *data = buffer->data;
    *format = buffer->format;
    *stride = buffer->stride;
    return true;
}

static void wlr_non_owning_data_buffer_end_data_ptr_access(wlr_buffer* /*wlr_buf*/)
{
    // This space is intentionally left blank
}

static wlr_buffer_impl const wlr_non_owning_data_buffer_impl = {
    .destroy = wlr_non_owning_data_buffer_destroy,
    .begin_data_ptr_access = wlr_non_owning_data_buffer_begin_data_ptr_access,
    .end_data_ptr_access = wlr_non_owning_data_buffer_end_data_ptr_access,
};

static inline wlr_non_owning_data_buffer* wlr_non_owning_data_buffer_create(uint32_t width,
                                                                            uint32_t height,
                                                                            uint32_t format,
                                                                            uint32_t stride,
                                                                            void* data)
{
    assert(data);

    auto buffer
        = static_cast<wlr_non_owning_data_buffer*>(calloc(1, sizeof(wlr_non_owning_data_buffer)));
    if (buffer == NULL) {
        return NULL;
    }

    wlr_buffer_init(&buffer->base, &wlr_non_owning_data_buffer_impl, width, height);
    buffer->format = format;
    buffer->stride = stride;

    buffer->data = data;

    return buffer;
}

}
