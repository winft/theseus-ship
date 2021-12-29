/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include "surface.h"
#include "utils.h"

#include <wayland_logging.h>

#include <cassert>
#include <cstring>
#include <unistd.h>

namespace KWin::render::backend::wlroots
{

wlr_buffer_override* get_buffer_override(wlr_buffer* buffer);

buffer* get_buffer(wlr_buffer* buffer)
{
    return get_buffer_override(buffer)->front;
}

void buffer_destroy(wlr_buffer* wlr_buffer)
{
    auto buffer = get_buffer(wlr_buffer);
    delete buffer;
}

bool buffer_get_dmabuf(wlr_buffer* wlr_buffer, wlr_dmabuf_attributes* attribs)
{
    auto buffer = get_buffer(wlr_buffer);
    memcpy(attribs, &buffer->dmabuf, sizeof(buffer->dmabuf));
    return true;
}

constexpr wlr_buffer_impl buffer_impl = {
    .destroy = buffer_destroy,
    .get_dmabuf = buffer_get_dmabuf,
    nullptr,
    nullptr,
};

constexpr wlr_buffer_impl buffer_headless_impl = {
    .destroy = buffer_destroy,
    .get_dmabuf = nullptr,
    nullptr,
    nullptr,
};

wlr_buffer_override* get_buffer_override(wlr_buffer* buffer)
{
    assert(buffer->impl == &buffer_impl || buffer->impl == &buffer_headless_impl);
    return reinterpret_cast<wlr_buffer_override*>(buffer);
}

buffer::buffer(surface* surf, bool headless)
    : surf{surf}
{
    if (headless) {
        wlr_buffer_init(
            &native.base, &buffer_headless_impl, surf->size.width(), surf->size.height());
        return;
    }

    bo = gbm_surface_lock_front_buffer(surf->gbm);
    if (!bo) {
        qCWarning(KWIN_WL) << "Locking front buffer failed.";
        // TODO(romangg): throw
        return;
    }

    if (!set_dmabuf_attributes()) {
        qCWarning(KWIN_WL) << "Setting dmabuf attributes failed.";
        // TODO(romangg): throw
        return;
    }

    surf->buffers.push_back(this);

    native.front = this;
    native.base.width = dmabuf.width;
    native.base.height = dmabuf.height;

    gbm_bo_set_user_data(bo, this, nullptr);
    wlr_buffer_init(&native.base, &buffer_impl, dmabuf.width, dmabuf.height);
}

buffer::~buffer()
{
    wlr_dmabuf_attributes_finish(&dmabuf);
    if (surf) {
        remove_all(surf->buffers, this);
        if (bo) {
            gbm_surface_release_buffer(surf->gbm, bo);
        }
    }
}

QSize buffer::size() const
{
    return QSize(native.base.width, native.base.height);
}

bool buffer::set_dmabuf_attributes()
{
    wlr_dmabuf_attributes attribs = {};

    attribs.n_planes = gbm_bo_get_plane_count(bo);
    if (attribs.n_planes > WLR_DMABUF_MAX_PLANES) {
        qCWarning(KWIN_WL, "GBM BO contains too many planes (%d)", attribs.n_planes);
        return false;
    }

    attribs.width = gbm_bo_get_width(bo);
    attribs.height = gbm_bo_get_height(bo);
    attribs.format = gbm_bo_get_format(bo);
    attribs.modifier = gbm_bo_get_modifier(bo);

    auto handle_error = [&attribs](int max_fd) {
        for (int i = 0; i < max_fd; ++i) {
            close(attribs.fd[i]);
        }
    };

    int32_t handle = -1;
    for (int i = 0; i < attribs.n_planes; ++i) {
        // GBM is lacking a function to get a FD for a given plane. Instead,
        // check all planes have the same handle. We can't use
        // drmPrimeHandleToFD because that messes up handle ref'counting in
        // the user-space driver.
        // TODO: use gbm_bo_get_plane_fd when it lands, see
        // https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/5442
        union gbm_bo_handle plane_handle = gbm_bo_get_handle_for_plane(bo, i);
        if (plane_handle.s32 < 0) {
            qCWarning(KWIN_WL, "gbm_bo_get_handle_for_plane failed");
            handle_error(i);
            return false;
        }
        if (i == 0) {
            handle = plane_handle.s32;
        } else if (plane_handle.s32 != handle) {
            qCWarning(KWIN_WL,
                      "Failed to export GBM BO: all planes don't have the same GEM handle");
            handle_error(i);
            return false;
        }

        attribs.fd[i] = gbm_bo_get_fd(bo);
        if (attribs.fd[i] < 0) {
            qCWarning(KWIN_WL, "gbm_bo_get_fd failed");
            handle_error(i);
            return false;
        }

        attribs.offset[i] = gbm_bo_get_offset(bo, i);
        attribs.stride[i] = gbm_bo_get_stride_for_plane(bo, i);
    }

    dmabuf = attribs;
    return true;
}

}
