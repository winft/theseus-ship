/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "blur.h"
#include "blurshader.h"

// KConfigSkeleton
#include "blurconfig.h"

#include <render/effect/interface/effect_frame.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/vertex_buffer.h>

#include <QGuiApplication>
#include <QMatrix4x4>
#include <QScreen> // for QGuiApplication
#include <QTime>
#include <cmath> // for ceil()
#include <cstdlib>

#include <KConfigGroup>
#include <KSharedConfig>

#include <KDecoration2/Decoration>

namespace KWin
{

void update_function(BlurEffect& effect, KWin::effect::region_update const& update)
{
    if (!update.base.window) {
        // Reset requested
        effect.reset();
        return;
    }

    if (update.base.valid) {
        effect.blur_regions[update.base.window] = update.value;
    } else {
        effect.blur_regions.remove(update.base.window);
    }
}

BlurEffect::BlurEffect()
{
    initConfig<BlurConfig>();
    shader = new BlurShader(this);

    init_blur_strength_values();
    reconfigure(ReconfigureAll);

    QObject::connect(effects, &EffectsHandler::screenAdded, this, &BlurEffect::handle_screen_added);
    QObject::connect(
        effects, &EffectsHandler::screenRemoved, this, &BlurEffect::handle_screen_removed);
    for (auto&& screen : effects->screens()) {
        handle_screen_added(screen);
    }

    if (shader && shader->isValid() && render_targets_are_valid) {
        auto& blur_integration = effects->get_blur_integration();
        auto update = [this](auto&& data) { update_function(*this, data); };
        blur_integration.add(*this, update);
    }
}

BlurEffect::~BlurEffect() = default;

void BlurEffect::reset()
{
    effects->makeOpenGLContextCurrent();
    update_texture();
    effects->doneOpenGLContextCurrent();
}

static bool check_render_targets_are_valid(std::vector<blur_render_target> const& targets)
{
    return !targets.empty() && std::all_of(targets.cbegin(), targets.cend(), [](auto&& target) {
        return target.fbo->valid();
    });
}

void BlurEffect::update_texture()
{
    render_targets_are_valid = true;
    for (auto& [key, data] : render_screens) {
        update_texture(data);
        render_targets_are_valid &= check_render_targets_are_valid(data.targets);
    }
}

void BlurEffect::update_texture(blur_render_data& screen)
{
    screen.targets.clear();

    /* Reserve memory for:
     *  - The original sized texture (1)
     *  - The downsized textures (downsample_count)
     *  - The helper texture (1)
     */
    screen.targets.reserve(downsample_count + 2);

    GLenum textureFormat = GL_RGBA8;

    // Check the color encoding of the default framebuffer
    if (!GLPlatform::instance()->isGLES()) {
        GLuint prevFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&prevFbo));

        if (prevFbo != 0) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }

        GLenum colorEncoding = GL_LINEAR;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                                              GL_BACK_LEFT,
                                              GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
                                              reinterpret_cast<GLint*>(&colorEncoding));

        if (prevFbo != 0) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
        }

        if (colorEncoding == GL_SRGB) {
            textureFormat = GL_SRGB8_ALPHA8;
        }
    }

    auto const screen_size = screen.screen.geometry().size();
    for (int i = 0; i <= downsample_count; i++) {
        screen.targets.emplace_back(
            std::make_unique<GLTexture>(textureFormat, screen_size / (1 << i)));
    }

    // This last set is used as a temporary helper texture
    screen.targets.emplace_back(std::make_unique<GLTexture>(textureFormat, screen_size));

    screen.stack = {};

    // Upsample
    for (int i = 1; i < downsample_count; i++) {
        screen.stack.push(screen.targets.at(i).fbo.get());
    }

    // Downsample
    for (int i = downsample_count; i > 0; i--) {
        screen.stack.push(screen.targets.at(i).fbo.get());
    }

    // Copysample (with the original sized target)
    screen.stack.push(screen.targets.front().fbo.get());

    // Invalidate noise texture
    noise_texture = {};
}

void BlurEffect::init_blur_strength_values()
{
    // This function creates an array of blur strength values that are evenly distributed.

    // The range of the slider on the blur settings UI
    int numOfBlurSteps = 15;
    int remainingSteps = numOfBlurSteps;

    /*
     * Explanation for these numbers:
     *
     * The texture blur amount depends on the downsampling iterations and the offset value.
     * By changing the offset we can alter the blur amount without relying on further downsampling.
     * But there is a minimum and maximum value of offset per downsample iteration before we
     * get artifacts.
     *
     * The offset min variable is the minimum offset value for an iteration before we
     * get blocky artifacts because of the downsampling.
     *
     * The offset max value is the maximum offset value for an iteration before we
     * get diagonal line artifacts because of the nature of the dual kawase blur algorithm.
     *
     * The expand value is the minimum value for an iteration before we reach the end
     * of a texture in the shader and sample outside of the area that was copied into the
     * texture from the screen.
     */

    // {min, max, expand}
    blur_offsets.append({1.0, 2.0, 10});  // Down sample size / 2
    blur_offsets.append({2.0, 3.0, 20});  // Down sample size / 4
    blur_offsets.append({2.0, 5.0, 50});  // Down sample size / 8
    blur_offsets.append({3.0, 8.0, 150}); // Down sample size / 16
    // blur_offsets.append({5.0, 10.0, 400}); // Down sample size / 32
    // blur_offsets.append({7.0, ?.0});       // Down sample size / 64

    float offsetSum = 0;

    for (int i = 0; i < blur_offsets.size(); i++) {
        offsetSum += blur_offsets[i].max - blur_offsets[i].min;
    }

    for (int i = 0; i < blur_offsets.size(); i++) {
        int iterationNumber
            = std::ceil((blur_offsets[i].max - blur_offsets[i].min) / offsetSum * numOfBlurSteps);
        remainingSteps -= iterationNumber;

        if (remainingSteps < 0) {
            iterationNumber += remainingSteps;
        }

        auto offsetDifference = blur_offsets[i].max - blur_offsets[i].min;

        for (int j = 1; j <= iterationNumber; j++) {
            // {iteration, offset}
            blur_strength_values.append(
                {i + 1, blur_offsets[i].min + (offsetDifference / iterationNumber) * j});
        }
    }
}

void BlurEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    BlurConfig::self()->read();

    auto blur_strength = BlurConfig::blurStrength() - 1;
    downsample_count = blur_strength_values[blur_strength].iteration;
    offset = blur_strength_values[blur_strength].offset;
    expand_limit = blur_offsets[downsample_count - 1].expand;
    noise_strength = BlurConfig::noiseStrength();
    scaling_factor = qMax(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0);

    update_texture();

    if (!shader || !shader->isValid()) {
        effects->get_blur_integration().remove(*this);
    }

    // Update all windows for the blur to take effect.
    effects->addRepaintFull();
}

bool BlurEffect::enabledByDefault()
{
    GLPlatform* gl = GLPlatform::instance();

    if (gl->isIntel() && gl->chipClass() < SandyBridge)
        return false;
    if (gl->isPanfrost() && gl->chipClass() <= MaliT8XX) {
        return false;
    }
    // The blur effect works, but is painfully slow (FPS < 5) on Mali and VideoCore
    if (gl->isLima() || gl->isVideoCore4() || gl->isVideoCore3D()) {
        return false;
    }
    if (gl->isSoftwareEmulation()) {
        return false;
    }

    return true;
}

bool BlurEffect::supported()
{
    return effects->isOpenGLCompositing() && GLFramebuffer::supported()
        && GLFramebuffer::blitSupported();
}

void BlurEffect::handle_screen_added(EffectScreen const* screen)
{
    render_screens.insert({screen, blur_render_data{*screen, {}, {}}});
    QObject::connect(screen, &EffectScreen::geometryChanged, this, [this] { reset(); });
    update_texture();
}

void BlurEffect::handle_screen_removed(EffectScreen const* screen)
{
    render_screens.erase(screen);
    update_texture();
}

bool BlurEffect::deco_supports_blur_behind(EffectWindow const* win) const
{
    return win->decoration() && !win->decoration()->blurRegion().isNull();
}

QRegion BlurEffect::deco_blur_region(EffectWindow const* win) const
{
    if (!deco_supports_blur_behind(win)) {
        return QRegion();
    }

    auto const decorationRegion = QRegion(win->decoration()->rect()) - win->decorationInnerRect();

    // We return only blurred regions that belong to decoration region.
    return decorationRegion.intersected(win->decoration()->blurRegion());
}

QRect BlurEffect::expand(QRect const& rect) const
{
    return rect.adjusted(-expand_limit, -expand_limit, expand_limit, expand_limit);
}

QRegion BlurEffect::expand(QRegion const& region) const
{
    QRegion expanded;

    for (auto const& rect : region) {
        expanded += expand(rect);
    }

    return expanded;
}

QRegion BlurEffect::blur_region(EffectWindow const* win) const
{
    auto it = blur_regions.find(win);

    if (it == blur_regions.end()) {
        // If the client hasn't specified a blur region, we'll only enable the effect behind deco.
        return win->decorationHasAlpha() && deco_supports_blur_behind(win) ? deco_blur_region(win)
                                                                           : QRegion{};
    }

    auto const& app_region = *it;
    if (app_region.isEmpty()) {
        // An empty region means that the blur effect should be enabled for the whole window.
        return win->rect();
    }

    auto region = app_region.translated(win->contentsRect().topLeft()) & win->decorationInnerRect();
    if (win->decorationHasAlpha() && deco_supports_blur_behind(win)) {
        region |= deco_blur_region(win);
    }

    return region;
}

void BlurEffect::upload_region(std::span<QVector2D> const map, size_t& index, QRegion const& region)
{
    for (auto const& r : region) {
        QVector2D const topLeft(r.x(), r.y());
        QVector2D const topRight((r.x() + r.width()), r.y());
        QVector2D const bottomLeft(r.x(), (r.y() + r.height()));
        QVector2D const bottomRight((r.x() + r.width()), (r.y() + r.height()));

        // First triangle
        map[index++] = topRight;
        map[index++] = topLeft;
        map[index++] = bottomLeft;

        // Second triangle
        map[index++] = bottomLeft;
        map[index++] = bottomRight;
        map[index++] = topRight;
    }
}

void BlurEffect::upload_geometry(GLVertexBuffer* vbo,
                                 QRegion const& expanded_blur_region,
                                 QRegion const& blur_region)
{
    auto const vertexCount = (expanded_blur_region.rectCount() + blur_region.rectCount()) * 6;
    if (!vertexCount) {
        return;
    }

    auto map = vbo->map<QVector2D>(vertexCount);
    if (!map) {
        return;
    }

    size_t index = 0;
    upload_region(*map, index, expanded_blur_region);
    upload_region(*map, index, blur_region);

    vbo->unmap();

    constexpr std::array layout{
        GLVertexAttrib{
            .attributeIndex = VA_Position,
            .componentCount = 2,
            .type = GL_FLOAT,
            .relativeOffset = 0,
        },
        GLVertexAttrib{
            .attributeIndex = VA_TexCoord,
            .componentCount = 2,
            .type = GL_FLOAT,
            .relativeOffset = 0,
        },
    };
    vbo->setAttribLayout(std::span(layout), sizeof(QVector2D));
}

void BlurEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    painted_area = {};
    current_blur_area = {};

    current_screen = &data.screen;
    effects->prePaintScreen(data);
}

void BlurEffect::postPaintScreen()
{
    current_screen = nullptr;
    effects->postPaintScreen();
}

void BlurEffect::prePaintWindow(effect::window_prepaint_data& data)
{
    // This effect relies on prePaintWindow being called in the bottom to top order.

    effects->prePaintWindow(data);

    if (!shader || !shader->isValid()) {
        return;
    }

    auto const oldClip = data.clip;
    if (data.clip.intersects(current_blur_area)) {
        // to blur an area partially we have to shrink the opaque area of a window
        QRegion newClip;
        for (auto const& rect : data.clip) {
            newClip |= rect.adjusted(expand_limit, expand_limit, -expand_limit, -expand_limit);
        }
        data.clip = newClip;

        // we don't have to blur a region we don't see
        current_blur_area -= newClip;
    }

    // If we have to paint a non-opaque part of this window that intersects with the currently
    // blurred region we have to redraw the whole region.
    if ((data.paint.region - oldClip).intersects(current_blur_area)) {
        data.paint.region |= current_blur_area;
    }

    // in case this window has regions to be blurred
    assert(current_screen);
    auto const screen_geo = current_screen->geometry();
    auto const blurArea = blur_region(&data.window).translated(data.window.pos()) & screen_geo;
    auto const expandedBlur = (data.window.isDock() ? blurArea : expand(blurArea)) & screen_geo;

    // if this window or a window underneath the blurred area is painted again we have to
    // blur everything
    if (painted_area.intersects(expandedBlur) || data.paint.region.intersects(blurArea)) {
        data.paint.region |= expandedBlur;
        // we have to check again whether we do not damage a blurred area
        // of a window
        if (expandedBlur.intersects(current_blur_area)) {
            data.paint.region |= current_blur_area;
        }
    }

    current_blur_area |= expandedBlur;

    painted_area -= data.clip;
    painted_area |= data.paint.region;
}

bool BlurEffect::should_blur(effect::window_paint_data const& data) const
{
    if (!render_targets_are_valid || !shader || !shader->isValid()) {
        return false;
    }
    if (effects->activeFullScreenEffect() && !data.window.data(WindowForceBlurRole).toBool()) {
        return false;
    }
    if (data.window.isDesktop()) {
        return false;
    }

    auto const scaled = !qFuzzyCompare(data.paint.geo.scale.x(), 1.f)
        || !qFuzzyCompare(data.paint.geo.scale.y(), 1.f);
    auto const translated = data.paint.geo.translation.x() || data.paint.geo.translation.y();

    if ((scaled || (translated || (data.paint.mask & PAINT_WINDOW_TRANSFORMED)))
        && !data.window.data(WindowForceBlurRole).toBool()) {
        return false;
    }

    return true;
}

void BlurEffect::drawWindow(effect::window_paint_data& data)
{
    if (!should_blur(data)) {
        effects->drawWindow(data);
        return;
    }

    auto shape = blur_region(&data.window).translated(data.window.pos());

    // let's do the evil parts - someone wants to blur behind a transformed window
    if (!qFuzzyCompare(data.paint.geo.scale.x(), 1.f)
        || !qFuzzyCompare(data.paint.geo.scale.y(), 1.f)) {
        auto pt = shape.boundingRect().topLeft();
        QRegion scaled_shape;

        for (auto const& r : shape) {
            QPointF const topLeft(pt.x() + (r.x() - pt.x()) * data.paint.geo.scale.x()
                                      + data.paint.geo.translation.x(),
                                  pt.y() + (r.y() - pt.y()) * data.paint.geo.scale.y()
                                      + data.paint.geo.translation.y());
            QPoint const bottomRight(
                std::floor(topLeft.x() + r.width() * data.paint.geo.scale.x()) - 1,
                std::floor(topLeft.y() + r.height() * data.paint.geo.scale.y()) - 1);
            scaled_shape
                |= QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        }

        shape = scaled_shape;

    } else if (data.paint.geo.translation.x() || data.paint.geo.translation.y()) {
        // Only translated, not scaled
        QRegion translated;
        for (auto const& r : shape) {
            auto const t = QRectF(r).translated(data.paint.geo.translation.x(),
                                                data.paint.geo.translation.y());
            QPoint const topLeft(std::ceil(t.x()), std::ceil(t.y()));
            QPoint const bottomRight(std::floor(t.x() + t.width() - 1),
                                     std::floor(t.y() + t.height() - 1));
            translated |= QRect(topLeft, bottomRight);
        }
        shape = translated;
    }

    auto modal = data.window.transientFor();
    auto const transientForIsDock = (modal ? modal->isDock() : false);

    do_blur(data,
            shape & data.paint.region & current_screen->geometry(),
            data.window.isDock() || transientForIsDock);

    // Draw the window over the blurred area
    effects->drawWindow(data);
}

void BlurEffect::generate_noise_texture()
{
    if (noise_strength == 0) {
        return;
    }

    // Init randomness based on time
    std::srand(static_cast<uint>(QTime::currentTime().msec()));

    QImage noise_image(QSize(256, 256), QImage::Format_Grayscale8);

    for (int y = 0; y < noise_image.height(); y++) {
        uint8_t* noise_imageLine = static_cast<uint8_t*>(noise_image.scanLine(y));

        for (int x = 0; x < noise_image.width(); x++) {
            noise_imageLine[x] = std::rand() % noise_strength;
        }
    }

    // The noise texture looks distorted when not scaled with integer
    noise_image = noise_image.scaled(noise_image.size() * scaling_factor);

    noise_texture = GLTexture(noise_image);
    noise_texture.setFilter(GL_NEAREST);
    noise_texture.setWrapMode(GL_REPEAT);
}

static QMatrix4x4 get_screen_projection(blur_render_data const& data)
{
    auto const& geo = data.screen.geometry();
    QMatrix4x4 proj;
    proj.ortho(geo.x(), geo.x() + geo.width(), geo.y() + geo.height(), geo.y(), 0, 65535);
    return proj;
}

void BlurEffect::do_blur(effect::window_paint_data& data, QRegion const& shape, bool isDock)
{
    if (shape.isEmpty()) {
        return;
    }

    auto const opacity = data.paint.opacity * data.window.opacity();

    assert(current_screen);
    auto const& screen_data = render_screens.at(current_screen);
    auto const screen_geo = current_screen->geometry();
    auto const expanded_blur_region = expand(shape) & expand(screen_geo);
    auto const use_srgb = screen_data.targets.front().texture->internalFormat() == GL_SRGB8_ALPHA8;

    if (use_srgb) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }

    // Upload geometry for the down and upsample iterations
    auto vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();

    upload_geometry(vbo, expanded_blur_region, shape);

    auto const logicalSourceRect = expanded_blur_region.boundingRect() & screen_geo;
    int blurRectCount = expanded_blur_region.rectCount() * 6;

    /*
     * If the window is a dock or panel we avoid the "extended blur" effect.
     * Extended blur is when windows that are not under the blurred area affect
     * the final blur result.
     * We want to avoid this on panels, because it looks really weird and ugly
     * when maximized windows or windows near the panel affect the dock blur.
     */
    if (isDock) {
        screen_data.targets.back().fbo->blit_from_current_render_target(
            data.render, logicalSourceRect, logicalSourceRect.translated(-screen_geo.topLeft()));
        render::push_framebuffers(data.render, screen_data.stack);

        vbo->bindArrays();
        copy_screen_sample_texture(
            data.render, screen_data, vbo, blurRectCount, shape.boundingRect());
    } else {
        screen_data.targets.front().fbo->blit_from_current_render_target(
            data.render, logicalSourceRect, logicalSourceRect.translated(-screen_geo.topLeft()));
        render::push_framebuffers(data.render, screen_data.stack);

        // Remove the screen_data.targets.front() from the top of the stack that we will not use.
        render::pop_framebuffer(data.render);
    }

    vbo->bindArrays();
    downsample_texture(data.render, screen_data, vbo, blurRectCount);
    upsample_texture(data.render, screen_data, vbo, blurRectCount);

    // Modulate the blurred texture with the window opacity if the window isn't opaque
    if (opacity < 1.0) {
        glEnable(GL_BLEND);
#if 1 // bow shape, always above y = x
        float o = 1.0f - opacity;
        o = 1.0f - o * o;
#else // sigmoid shape, above y = x for x > 0.5, below y = x for x < 0.5
        float o = 2.0f * opacity - 1.0f;
        o = 0.5f + o / (1.0f + qAbs(o));
#endif
        glBlendColor(0, 0, 0, o);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    }

    upsample_to_screen(screen_data, data, vbo, blurRectCount, shape.rectCount() * 6);

    if (use_srgb) {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    if (opacity < 1.0) {
        glDisable(GL_BLEND);
    }

    if (noise_strength > 0) {
        // Apply an additive noise onto the blurred image.
        // The noise is useful to mask banding artifacts, which often happens due to the smooth
        // color transitions in the blurred image. The noise is applied in perceptual space (i.e.
        // after glDisable(GL_FRAMEBUFFER_SRGB)). This practice is also seen in other application of
        // noise synthesis (films, image codecs), and makes the noise less visible overall (reduces
        // graininess).
        glEnable(GL_BLEND);
        if (opacity < 1.0) {
            // We need to modulate the opacity of the noise as well; otherwise a thin layer would
            // appear when applying effects like fade out. glBlendColor should have been set above.
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
        } else {
            // Add the shader's output directly to the pixels in framebuffer.
            glBlendFunc(GL_ONE, GL_ONE);
        }

        apply_noise(data, vbo, blurRectCount, shape.rectCount() * 6);
        glDisable(GL_BLEND);
    }

    vbo->unbindArrays();
}

void BlurEffect::upsample_to_screen(blur_render_data const& data,
                                    effect::window_paint_data const& win_data,
                                    GLVertexBuffer* vbo,
                                    int vboStart,
                                    int blurRectCount)
{
    auto const& tex = data.targets.at(1).texture;
    tex->bind();

    shader->bind(BlurShader::UpSampleType);

    QMatrix4x4 fragCoordToUv;
    auto const output_geo = data.screen.geometry();

    auto const vp_matrix = effect::get_viewport_matrix(win_data.render);

    QMatrix4x4 inv_mvp;
    inv_mvp.translate(output_geo.x(), output_geo.y());
    inv_mvp.translate(output_geo.width() / 2., output_geo.height() / 2.);
    inv_mvp.scale(output_geo.width() / 2., output_geo.height() / 2.);

    if (win_data.render.flip_y) {
        // If already flipped we don't need to flip y-axis anymore for OpenGl texture.
        inv_mvp.scale(1, -1);
    }

    inv_mvp = inv_mvp * effect::get_transform_matrix(win_data.render.transform).inverted();

    QMatrix4x4 move_output;
    move_output.translate(-output_geo.x(), -output_geo.y());

    QMatrix4x4 fit_texture;
    fit_texture.scale(1.0 / output_geo.width(), 1.0 / output_geo.height());

    fragCoordToUv = fit_texture * move_output * inv_mvp * vp_matrix.inverted();

    shader->setFragCoordToUv(fragCoordToUv);
    shader->setTargetTextureSize(win_data.render.targets.top()->size());

    shader->setOffset(offset);

    // the vbo is in logical coordinates, adjust for that
    shader->setModelViewProjectionMatrix(effect::get_mvp(win_data));

    // Render to the screen
    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    shader->unbind();
}

void BlurEffect::apply_noise(effect::window_paint_data const& data,
                             GLVertexBuffer* vbo,
                             int vboStart,
                             int blurRectCount)
{
    if (noise_texture.isNull()) {
        generate_noise_texture();
    }

    shader->bind(BlurShader::NoiseSampleType);
    shader->setTargetTextureSize(data.render.targets.top()->size());
    shader->setNoiseTextureSize(noise_texture.size());
    shader->setTexturePosition(data.window.pos());

    noise_texture.bind();

    shader->setOffset(offset);

    // the vbo is in logical coordinates, adjust for that
    shader->setModelViewProjectionMatrix(effect::get_mvp(data));

    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    shader->unbind();
}

void BlurEffect::downsample_texture(effect::render_data& eff_data,
                                    blur_render_data const& data,
                                    GLVertexBuffer* vbo,
                                    int blurRectCount)
{
    shader->bind(BlurShader::DownSampleType);
    shader->setOffset(offset);
    shader->setModelViewProjectionMatrix(get_screen_projection(data));

    for (int i = 1; i <= downsample_count; i++) {
        shader->setTargetTextureSize(data.targets[i].texture->size());

        // Copy the image from this texture
        data.targets[i - 1].texture->bind();

        vbo->draw(GL_TRIANGLES, 0, blurRectCount);
        render::pop_framebuffer(eff_data);
    }

    shader->unbind();
}

void BlurEffect::upsample_texture(effect::render_data& eff_data,
                                  blur_render_data const& data,
                                  GLVertexBuffer* vbo,
                                  int blurRectCount)
{
    shader->bind(BlurShader::UpSampleType);
    shader->setOffset(offset);
    shader->setModelViewProjectionMatrix(get_screen_projection(data));

    for (int i = downsample_count - 1; i >= 1; i--) {
        auto const& tex = data.targets.at(i).texture;

        QMatrix4x4 fragCoordToUv;
        fragCoordToUv.scale(1.0 / tex->width(), 1.0 / tex->height());
        shader->setFragCoordToUv(fragCoordToUv);
        shader->setTargetTextureSize(tex->size());

        // Copy the image from this texture
        data.targets[i + 1].texture->bind();

        vbo->draw(GL_TRIANGLES, 0, blurRectCount);
        render::pop_framebuffer(eff_data);
    }

    shader->unbind();
}

void BlurEffect::copy_screen_sample_texture(effect::render_data& eff_data,
                                            blur_render_data const& data,
                                            GLVertexBuffer* vbo,
                                            int blurRectCount,
                                            QRect const& boundingRect)
{
    auto const& screen_size = data.screen.geometry().size();

    shader->bind(BlurShader::CopySampleType);
    shader->setModelViewProjectionMatrix(get_screen_projection(data));
    shader->setTargetTextureSize(screen_size);

    /*
     * This '1' sized adjustment is necessary do avoid windows affecting the blur that are
     * right next to this window.
     */
    shader->setBlurRect(boundingRect.adjusted(1, 1, -1, -1), screen_size);
    data.targets.back().texture->bind();

    vbo->draw(GL_TRIANGLES, 0, blurRectCount);
    render::pop_framebuffer(eff_data);

    shader->unbind();
}

bool BlurEffect::provides(Effect::Feature feature)
{
    if (feature == Blur) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

bool BlurEffect::isActive() const
{
    return !effects->isScreenLocked();
}

}
