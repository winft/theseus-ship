/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <QStack>
#include <QVector2D>
#include <QVector>
#include <vector>

namespace KWin
{

static const int borderSize = 5;

class BlurShader;

struct blur_render_target {
    blur_render_target(std::unique_ptr<GLTexture> tex)
        : texture{std::move(tex)}
        , fbo{std::make_unique<GLFramebuffer>(texture.get())}
    {
        texture->setFilter(GL_LINEAR);
        texture->setWrapMode(GL_CLAMP_TO_EDGE);
    }

    std::unique_ptr<GLTexture> texture;
    std::unique_ptr<GLFramebuffer> fbo;
};

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

    QMap<EffectWindow const*, QRegion> blur_regions;

private:
    QRect expand(QRect const& rect) const;
    QRegion expand(QRegion const& region) const;
    void init_blur_strength_values();
    void update_texture();
    QRegion blur_region(EffectWindow const* win) const;
    QRegion deco_blur_region(EffectWindow const* win) const;
    bool deco_supports_blur_behind(EffectWindow const* win) const;
    bool should_blur(effect::window_paint_data const& data) const;
    void do_blur(effect::window_paint_data const& data, QRegion const& shape, bool isDock);
    void upload_region(QVector2D*& map, QRegion const& region);
    void upload_geometry(GLVertexBuffer* vbo,
                         QRegion const& expanded_blur_region,
                         QRegion const& blur_region);
    void generate_noise_texture();

    void upsample_to_screen(effect::window_paint_data const& data,
                            GLVertexBuffer* vbo,
                            int vboStart,
                            int blurRectCount);
    void apply_noise(effect::window_paint_data const& data,
                     GLVertexBuffer* vbo,
                     int vboStart,
                     int blurRectCount);
    void downsample_texture(GLVertexBuffer* vbo, int blurRectCount, QMatrix4x4 const& mvp);
    void upsample_texture(GLVertexBuffer* vbo, int blurRectCount, QMatrix4x4 const& mvp);
    void copy_screen_sample_texture(QSize const& viewport,
                                    QSize const& fboSize,
                                    GLVertexBuffer* vbo,
                                    int blurRectCount,
                                    QRect const& boundingRect,
                                    QMatrix4x4 const& mvp);

private:
    BlurShader* shader;
    std::vector<blur_render_target> render_targets;
    QStack<GLFramebuffer*> render_target_stack;
    bool render_targets_are_valid{false};

    GLTexture noise_texture;

    // keeps track of all painted areas (from bottom to top)
    QRegion painted_area;
    // keeps track of the currently blured area of the windows(from bottom to top)
    QRegion current_blur_area;

    // number of times the texture will be downsized to half size
    int downsample_count;
    int offset;
    int expand_limit;
    int noise_strength;
    int scaling_factor;

    struct blur_offset_data {
        float min;
        float max;
        int expand;
    };
    QVector<blur_offset_data> blur_offsets;

    struct blur_strength_data {
        int iteration;
        float offset;
    };
    QVector<blur_strength_data> blur_strength_values;
};

}
