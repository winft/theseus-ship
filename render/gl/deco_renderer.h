/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "decorations/decorationrenderer.h"

#include <kwingl/utils.h>

#include <cmath>

namespace KWin::render::gl
{

class deco_renderer : public Decoration::Renderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int { Left, Top, Right, Bottom, Count };
    explicit deco_renderer(Decoration::DecoratedClientImpl* client);
    ~deco_renderer() override;

    void render() override;
    void reparent(Toplevel* window) override;

    GLTexture* texture()
    {
        return m_texture.data();
    }
    GLTexture* texture() const
    {
        return m_texture.data();
    }

private:
    void resizeTexture();
    QScopedPointer<GLTexture> m_texture;
};

}
