/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BLUR_H
#define BLUR_H

#include <kwineffects/effect.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <QStack>
#include <QVector2D>
#include <QVector>

namespace KWin
{

static const int borderSize = 5;

class BlurShader;

class BlurEffect : public KWin::Effect
{
    Q_OBJECT

public:
    BlurEffect();
    ~BlurEffect() override;

    static bool supported();
    static bool enabledByDefault();

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void drawWindow(effect::window_paint_data& data) override;

    bool provides(Feature feature) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 20;
    }

    void reset();

    QMap<EffectWindow const*, QRegion> blurRegions;

private:
    QRect expand(const QRect& rect) const;
    QRegion expand(const QRegion& region) const;
    bool renderTargetsValid() const;
    void initBlurStrengthValues();
    void updateTexture();
    QRegion blurRegion(const EffectWindow* w) const;
    QRegion decorationBlurRegion(const EffectWindow* w) const;
    bool decorationSupportsBlurBehind(const EffectWindow* w) const;
    bool shouldBlur(effect::window_paint_data const& data) const;
    void doBlur(effect::window_paint_data const& data,
                const QRegion& shape,
                const QRect& screen,
                bool isDock);
    void uploadRegion(QVector2D*& map, QRegion const& region, int const downSampleIterations);
    void
    uploadGeometry(GLVertexBuffer* vbo, const QRegion& blurRegion, const QRegion& windowRegion);
    void generateNoiseTexture();

    void upscaleRenderToScreen(GLVertexBuffer* vbo,
                               int vboStart,
                               int blurRectCount,
                               const QMatrix4x4& screenProjection);
    void applyNoise(GLVertexBuffer* vbo,
                    int vboStart,
                    int blurRectCount,
                    const QMatrix4x4& screenProjection,
                    QPoint windowPosition);
    void downSampleTexture(GLVertexBuffer* vbo, int blurRectCount);
    void upSampleTexture(GLVertexBuffer* vbo, int blurRectCount);
    void copyScreenSampleTexture(GLVertexBuffer* vbo,
                                 int blurRectCount,
                                 QRegion blurShape,
                                 const QMatrix4x4& screenProjection);

private:
    BlurShader* m_shader;
    std::vector<std::unique_ptr<GLFramebuffer>> m_renderTargets;
    std::vector<std::unique_ptr<GLTexture>> m_renderTextures;
    QStack<GLFramebuffer*> m_renderTargetStack;

    GLTexture m_noiseTexture;

    bool m_renderTargetsValid;
    QRegion m_paintedArea; // keeps track of all painted areas (from bottom to top)
    QRegion m_currentBlur; // keeps track of the currently blured area of the windows(from bottom to
                           // top)

    int m_downSampleIterations; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;
    int m_noiseStrength;
    int m_scalingFactor;

    struct OffsetStruct {
        float minOffset;
        float maxOffset;
        int expandSize;
    };

    QVector<OffsetStruct> blurOffsets;

    struct BlurValuesStruct {
        int iteration;
        float offset;
    };

    QVector<BlurValuesStruct> blurStrengthValues;
};

inline bool BlurEffect::provides(Effect::Feature feature)
{
    if (feature == Blur) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

} // namespace KWin

#endif
