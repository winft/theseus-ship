/*
SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_LOOKINGGLASS_H
#define KWIN_LOOKINGGLASS_H

#include <render/effect/interface/effect.h>

#include <memory>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class GLVertexBuffer;

/**
 * Enhanced magnifier
 */
class LookingGlassEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int initialRadius READ initialRadius)
public:
    LookingGlassEffect();
    ~LookingGlassEffect() override;

    void reconfigure(ReconfigureFlags) override;

    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void paintScreen(effect::screen_paint_data& data) override;
    bool isActive() const override;

    static bool supported();

    // for properties
    int initialRadius() const
    {
        return initialradius;
    }
    QRect magnifierArea() const;

public Q_SLOTS:
    void toggle();
    void zoomIn();
    void zoomOut();
    void slotMouseChanged(const QPoint& pos,
                          const QPoint& old,
                          Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons,
                          Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);
    void slotWindowDamaged();

private:
    bool loadData();
    double zoom;
    double target_zoom;
    bool polling; // Mouse polling
    int radius;
    int initialradius;
    std::unique_ptr<GLTexture> m_texture;
    std::unique_ptr<GLFramebuffer> m_fbo;
    std::unique_ptr<GLVertexBuffer> m_vbo;
    std::unique_ptr<GLShader> m_shader;
    std::chrono::milliseconds m_lastPresentTime;
    bool m_enabled;
    bool m_valid;
};

} // namespace

#endif
