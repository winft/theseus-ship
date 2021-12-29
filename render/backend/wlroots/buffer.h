/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include <QSize>
#include <gbm.h>

namespace KWin::render::backend::wlroots
{

class buffer;
class surface;

struct wlr_buffer_override {
    wlr_buffer base{};
    buffer* front{nullptr};
};

class buffer
{
public:
    buffer(surface* surf, bool headless);
    buffer(buffer const&) = delete;
    buffer& operator=(buffer const&) = delete;
    buffer(buffer&&) noexcept = default;
    buffer& operator=(buffer&&) noexcept = default;
    ~buffer();

    QSize size() const;

    gbm_bo* bo{nullptr};
    surface* surf;

    wlr_buffer_override native;
    wlr_dmabuf_attributes dmabuf;

private:
    bool set_dmabuf_attributes();
};

}
