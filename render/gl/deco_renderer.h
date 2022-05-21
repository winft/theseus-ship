/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco/renderer.h"

#include <kwingl/utils.h>

#include <cmath>

namespace KWin::render::gl
{

class deco_renderer : public win::deco::renderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int { Left, Top, Right, Bottom, Count };
    explicit deco_renderer(win::deco::client_impl* client);
    ~deco_renderer() override;

    void render() override;
    void reparent() override;

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
