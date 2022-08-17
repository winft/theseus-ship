/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/shadow.h"

namespace KWin::render::qpainter
{

class shadow : public render::shadow
{
public:
    shadow(render::window* window);
    ~shadow() override;

    QImage& shadowTexture()
    {
        return m_texture;
    }

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    QImage m_texture;
};

}
