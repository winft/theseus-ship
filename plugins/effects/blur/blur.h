/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_screen.h>
#include <render/gl/interface/framebuffer.h>
#include <render/gl/interface/platform.h>
#include <render/gl/interface/texture.h>

#include <QVector2D>
#include <QVector>
#include <stack>
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

struct blur_render_data {
    EffectScreen const& screen;
    std::vector<blur_render_target> targets;
    std::stack<GLFramebuffer*> stack;
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
    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void postPaintScreen() override;
    void prePaintWindow(effect::window_prepaint_data& data) override;
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
    void handle_screen_added(EffectScreen const* screen);
    void handle_screen_removed(EffectScreen const* screen);
    QRect expand(QRect const& rect) const;
    QRegion expand(QRegion const& region) const;
    void init_blur_strength_values();
    void update_texture();
    void update_texture(blur_render_data& data);
    QRegion blur_region(EffectWindow const* win) const;
    QRegion deco_blur_region(EffectWindow const* win) const;
    bool deco_supports_blur_behind(EffectWindow const* win) const;
    bool should_blur(effect::window_paint_data const& data) const;
    void do_blur(effect::window_paint_data& data, QRegion const& shape, bool isDock);
    void upload_region(std::span<QVector2D> const map, size_t& index, QRegion const& region);
    void upload_geometry(GLVertexBuffer* vbo,
                         QRegion const& expanded_blur_region,
                         QRegion const& blur_region);
    void generate_noise_texture();

    void upsample_to_screen(blur_render_data const& data,
                            effect::window_paint_data const& win_data,
                            GLVertexBuffer* vbo,
                            int vboStart,
                            int blurRectCount);
    void apply_noise(effect::window_paint_data const& data,
                     GLVertexBuffer* vbo,
                     int vboStart,
                     int blurRectCount);

    void downsample_texture(effect::render_data& eff_data,
                            blur_render_data const& data,
                            GLVertexBuffer* vbo,
                            int blurRectCount);
    void upsample_texture(effect::render_data& eff_data,
                          blur_render_data const& data,
                          GLVertexBuffer* vbo,
                          int blurRectCount);
    void copy_screen_sample_texture(effect::render_data& eff_data,
                                    blur_render_data const& data,
                                    GLVertexBuffer* vbo,
                                    int blurRectCount,
                                    QRect const& boundingRect);

    BlurShader* shader;

    std::unordered_map<EffectScreen const*, blur_render_data> render_screens;
    bool render_targets_are_valid{false};

    EffectScreen const* current_screen{nullptr};

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
