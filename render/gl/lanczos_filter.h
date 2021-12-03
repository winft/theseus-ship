/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2010 by Fredrik Höglund <fredrik@kde.org>
Copyright (C) 2010 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include <QBasicTimer>
#include <QObject>
#include <QVector2D>
#include <QVector4D>
#include <QVector>
#include <array>

namespace KWin
{
class EffectWindow;
class WindowPaintData;
class GLTexture;
class GLRenderTarget;
class GLShader;

namespace render
{
class effects_window_impl;
enum class paint_type;
class scene;

namespace gl
{

class lanczos_filter : public QObject
{
    Q_OBJECT

public:
    explicit lanczos_filter(render::scene* parent);
    ~lanczos_filter() override;
    void
    performPaint(effects_window_impl* w, paint_type mask, QRegion region, WindowPaintData& data);

protected:
    void timerEvent(QTimerEvent*) override;

private:
    void init();
    void updateOffscreenSurfaces();
    void setUniforms();
    void discardCacheTexture(EffectWindow* w);

    void createKernel(float delta, int* kernelSize);
    void createOffsets(int count, float width, Qt::Orientation direction);
    GLTexture* m_offscreenTex;
    GLRenderTarget* m_offscreenTarget;
    QBasicTimer m_timer;
    bool m_inited;
    QScopedPointer<GLShader> m_shader;
    int m_uOffsets;
    int m_uKernel;
    std::array<QVector2D, 16> m_offsets;
    std::array<QVector4D, 16> m_kernel;
    render::scene* m_scene;
};

}
}
}
