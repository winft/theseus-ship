/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect_frame.h"

#include "kwinglutils.h"

namespace KWin::render::gl
{

class scene;

class effect_frame : public render::effect_frame
{
public:
    effect_frame(effect_frame_impl* frame, gl::scene* scene);
    ~effect_frame() override;

    void free() override;
    void freeIconFrame() override;
    void freeTextFrame() override;
    void freeSelection() override;

    void render(QRegion region, double opacity, double frameOpacity) override;

    void crossFadeIcon() override;
    void crossFadeText() override;

    static void cleanup();

private:
    void updateTexture();
    void updateTextTexture();

    GLTexture* m_texture;
    GLTexture* m_textTexture;
    GLTexture* m_oldTextTexture;

    // need to keep the pixmap around to workaround some driver problems
    QPixmap* m_textPixmap;

    GLTexture* m_iconTexture;
    GLTexture* m_oldIconTexture;
    GLTexture* m_selectionTexture;
    GLVertexBuffer* m_unstyledVBO;
    scene* m_scene;

    static GLTexture* m_unstyledTexture;

    // need to keep the pixmap around to workaround some driver problems
    static QPixmap* m_unstyledPixmap;

    // Update OpenGL unstyled frame texture
    static void updateUnstyledTexture();
};

}
