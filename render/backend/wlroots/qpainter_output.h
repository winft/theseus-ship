/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/qpainter/backend.h"

#include <QImage>
#include <memory>

namespace KWin::render::backend::wlroots
{

class qpainter_backend;
class output;

class qpainter_output
{
public:
    qpainter_output(wlroots::output& output, qpainter_backend& backend);
    qpainter_output(qpainter_output const&) = delete;
    qpainter_output& operator=(qpainter_output const&) = delete;
    qpainter_output(qpainter_output&& other) noexcept = default;
    qpainter_output& operator=(qpainter_output&& other) noexcept = default;

    void begin_render();
    void present(QRegion const& damage);

    wlroots::output& output;
    qpainter_backend& backend;

    std::unique_ptr<QImage> buffer;
};

}
