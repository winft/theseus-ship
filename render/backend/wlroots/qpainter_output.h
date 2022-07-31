/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QImage>
#include <memory>

struct wlr_renderer;

namespace KWin::render::backend::wlroots
{

class output;

class KWIN_EXPORT qpainter_output
{
public:
    qpainter_output(wlroots::output& output, wlr_renderer* renderer);
    qpainter_output(qpainter_output const&) = delete;
    qpainter_output& operator=(qpainter_output const&) = delete;
    qpainter_output(qpainter_output&& other) noexcept = default;
    qpainter_output& operator=(qpainter_output&& other) noexcept = default;

    void begin_render();
    void present(QRegion const& damage);

    wlroots::output& output;
    wlr_renderer* renderer;

    std::unique_ptr<QImage> buffer;
};

}
