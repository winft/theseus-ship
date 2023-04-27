/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

class QImage;
class QRegion;

namespace KWin::render::qpainter
{

template<typename Scene>
class backend
{
public:
    using output_t = typename Scene::output_t;

    virtual ~backend() = default;

    virtual void begin_render(output_t& output) = 0;
    virtual void present(output_t* output, QRegion const& damage) = 0;

    virtual QImage* bufferForScreen(output_t* output) = 0;

    virtual bool needsFullRepaint() const = 0;
};

}
