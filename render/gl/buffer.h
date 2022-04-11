/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/buffer.h"

namespace KWin::render::gl
{

class scene;
class texture;

class buffer : public render::buffer
{
public:
    buffer(render::window* window, gl::scene* scene);
    ~buffer() override;
    render::gl::texture* texture() const;
    bool bind();
    bool isValid() const override;

private:
    QScopedPointer<render::gl::texture> m_texture;
    scene* m_scene;
};

}
