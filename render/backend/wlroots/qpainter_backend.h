/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/qpainter/backend.h"

namespace KWin::render::backend::wlroots
{

class platform;

class qpainter_backend : public qpainter::backend
{
public:
    qpainter_backend(wlroots::platform& platform);
    ~qpainter_backend() override;

    void begin_render(base::output& output) override;
    void present(base::output* output, QRegion const& damage) override;

    QImage* bufferForScreen(base::output* output) override;

    bool needsFullRepaint() const override;
    void tear_down();

    wlroots::platform& platform;
};

}
