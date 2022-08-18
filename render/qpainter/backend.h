/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

class QImage;
class QRegion;

namespace KWin
{
namespace base
{
class output;
}

namespace render::qpainter
{

class backend
{
public:
    virtual ~backend() = default;

    virtual void begin_render(base::output& output) = 0;
    virtual void present(base::output* output, QRegion const& damage) = 0;

    virtual QImage* bufferForScreen(base::output* output) = 0;

    virtual bool needsFullRepaint() const = 0;
};

}
}
