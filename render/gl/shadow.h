/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "scene.h"

#include "render/shadow.h"

#include <kwingl/utils.h>

#include <QSharedPointer>

namespace KWin
{

class Toplevel;

namespace render::gl
{

class shadow : public render::shadow
{
public:
    shadow(Toplevel* toplevel, gl::scene& scene);
    ~shadow() override;

    GLTexture* shadowTexture()
    {
        return m_texture.data();
    }

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    QSharedPointer<GLTexture> m_texture;
    gl::scene& scene;
};

}
}
