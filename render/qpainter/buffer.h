/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/buffer.h"

namespace KWin::render::qpainter
{

class buffer : public render::buffer
{
public:
    explicit buffer(render::window* window);
    ~buffer() override;

    void create() override;
    bool isValid() const override;

    void updateBuffer() override;
    QImage const& image();

private:
    QImage m_image;
};

}
