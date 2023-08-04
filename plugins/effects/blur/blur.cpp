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

#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>

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
    render_targets.clear();

    /* Reserve memory for:
     *  - The original sized texture (1)
     *  - The downsized textures (downsample_count)
     *  - The helper texture (1)
     */
    render_targets.reserve(downsample_count + 2);

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

    for (int i = 0; i <= downsample_count; i++) {
        render_targets.emplace_back(
            std::make_unique<GLTexture>(textureFormat, effects->virtualScreenSize() / (1 << i)));
    }

    // This last set is used as a temporary helper texture
    render_targets.emplace_back(
        std::make_unique<GLTexture>(textureFormat, effects->virtualScreenSize()));

    render_targets_are_valid = check_render_targets_are_valid(render_targets);
    render_target_stack.clear();
    render_target_stack.reserve(downsample_count * 2);

    // Upsample
    for (int i = 1; i < downsample_count; i++) {
        render_target_stack.push(render_targets.at(i).fbo.get());
    }

    // Downsample
    for (int i = downsample_count; i > 0; i--) {
        render_target_stack.push(render_targets.at(i).fbo.get());
    }

    // Copysample (with the original sized target)
    render_target_stack.push(render_targets.front().fbo.get());

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
    bool supported = effects->isOpenGLCompositing() && GLFramebuffer::supported()
        && GLFramebuffer::blitSupported();

    if (supported) {
        int maxTexSize;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);

        const QSize screenSize = effects->virtualScreenSize();
        if (screenSize.width() > maxTexSize || screenSize.height() > maxTexSize)
            supported = false;
    }
    return supported;
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

void BlurEffect::upload_region(QVector2D*& map,
                               QRegion const& region,
                               int const downSampleIterations)
{
    for (int i = 0; i <= downSampleIterations; i++) {
        auto const divisionRatio = (1 << i);

        for (auto const& r : region) {
            QVector2D const topLeft(r.x() / divisionRatio, r.y() / divisionRatio);
            QVector2D const topRight((r.x() + r.width()) / divisionRatio, r.y() / divisionRatio);
            QVector2D const bottomLeft(r.x() / divisionRatio, (r.y() + r.height()) / divisionRatio);
            QVector2D const bottomRight((r.x() + r.width()) / divisionRatio,
                                        (r.y() + r.height()) / divisionRatio);

            // First triangle
            *(map++) = topRight;
            *(map++) = topLeft;
            *(map++) = bottomLeft;

            // Second triangle
            *(map++) = bottomLeft;
            *(map++) = bottomRight;
            *(map++) = topRight;
        }
    }
}

void BlurEffect::upload_geometry(GLVertexBuffer* vbo,
                                 QRegion const& blur_region,
                                 QRegion const& windowRegion)
{
    auto const vertexCount
        = ((blur_region.rectCount() * (downsample_count + 1)) + windowRegion.rectCount()) * 6;
    if (!vertexCount) {
        return;
    }

    auto map = static_cast<QVector2D*>(vbo->map(vertexCount * sizeof(QVector2D)));

    upload_region(map, blur_region, downsample_count);
    upload_region(map, windowRegion, 0);

    vbo->unmap();

    GLVertexAttrib const layout[] = {{VA_Position, 2, GL_FLOAT, 0}, {VA_TexCoord, 2, GL_FLOAT, 0}};

    vbo->setAttribLayout(layout, 2, sizeof(QVector2D));
}

void BlurEffect::prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime)
{
    painted_area = {};
    current_blur_area = {};

    effects->prePaintScreen(data, presentTime);
}

void BlurEffect::prePaintWindow(effect::window_prepaint_data& data,
                                std::chrono::milliseconds presentTime)
{
    // This effect relies on prePaintWindow being called in the bottom to top order.

    effects->prePaintWindow(data, presentTime);

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
    auto const screen = effects->virtualScreenGeometry();
    auto const blurArea = blur_region(&data.window).translated(data.window.pos()) & screen;
    auto const expandedBlur = (data.window.isDock() ? blurArea : expand(blurArea)) & screen;

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

    auto const screen = effects->renderTargetRect();
    auto shape = blur_region(&data.window).translated(data.window.pos());

    // let's do the evil parts - someone wants to blur behind a transformed window
    if (!qFuzzyCompare(data.paint.geo.scale.x(), 1.f)
        || !qFuzzyCompare(data.paint.geo.scale.y(), 1.f)) {
        auto pt = shape.boundingRect().topLeft();
        QRegion scaled_shape;

        for (auto r : shape) {
            r.moveTo(pt.x() + (r.x() - pt.x()) * data.paint.geo.scale.x()
                         + data.paint.geo.translation.x(),
                     pt.y() + (r.y() - pt.y()) * data.paint.geo.scale.y()
                         + data.paint.geo.translation.y());
            r.setWidth(r.width() * data.paint.geo.scale.x());
            r.setHeight(r.height() * data.paint.geo.scale.y());
            scaled_shape |= r;
        }

        shape = scaled_shape;

    } else if (data.paint.geo.translation.x() || data.paint.geo.translation.y()) {
        // Only translated, not scaled
        shape = shape.translated(data.paint.geo.translation.x(), data.paint.geo.translation.y());
    }

    auto modal = data.window.transientFor();
    auto const transientForIsDock = (modal ? modal->isDock() : false);

    do_blur(data, shape & data.paint.region, screen, data.window.isDock() || transientForIsDock);

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

void BlurEffect::do_blur(effect::window_paint_data const& data,
                         QRegion const& shape,
                         QRect const& screen,
                         bool isDock)
{
    if (shape.isEmpty()) {
        return;
    }

    auto const windowRect = data.window.frameGeometry();
    auto const opacity = data.paint.opacity;

    // Blur would not render correctly on a secondary monitor because of wrong coordinates
    // BUG: 393723
    auto const xTranslate = -screen.x();
    auto const yTranslate = effects->virtualScreenSize().height() - screen.height() - screen.y();

    auto const expanded_blur_region = expand(shape) & expand(screen);
    auto const use_srgb = render_targets.front().texture->internalFormat() == GL_SRGB8_ALPHA8;

    // Upload geometry for the down and upsample iterations
    auto vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();

    upload_geometry(vbo, expanded_blur_region.translated(xTranslate, yTranslate), shape);
    vbo->bindArrays();

    auto const sourceRect = expanded_blur_region.boundingRect() & screen;
    auto const destRect = sourceRect.translated(xTranslate, yTranslate);
    int blurRectCount = expanded_blur_region.rectCount() * 6;

    /*
     * If the window is a dock or panel we avoid the "extended blur" effect.
     * Extended blur is when windows that are not under the blurred area affect
     * the final blur result.
     * We want to avoid this on panels, because it looks really weird and ugly
     * when maximized windows or windows near the panel affect the dock blur.
     */
    if (isDock) {
        render_targets.back().fbo->blitFromFramebuffer(effects->mapToRenderTarget(sourceRect),
                                                       destRect);
        GLFramebuffer::pushRenderTargets(render_target_stack);

        if (use_srgb) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }

        auto const screenRect = effects->virtualScreenGeometry();
        QMatrix4x4 mvp;
        mvp.ortho(0, screenRect.width(), screenRect.height(), 0, 0, 65535);
        copy_screen_sample_texture(
            vbo, blurRectCount, shape.translated(xTranslate, yTranslate), mvp);
    } else {
        render_targets.front().fbo->blitFromFramebuffer(effects->mapToRenderTarget(sourceRect),
                                                        destRect);
        GLFramebuffer::pushRenderTargets(render_target_stack);

        if (use_srgb) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }

        // Remove the render_targets.front() from the top of the stack that we will not use.
        GLFramebuffer::popRenderTarget();
    }

    downsample_texture(vbo, blurRectCount);
    upsample_texture(vbo, blurRectCount);

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

    upsample_to_screen(vbo,
                       blurRectCount * (downsample_count + 1),
                       shape.rectCount() * 6,
                       data.paint.screen_projection_matrix);

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
        apply_noise(vbo,
                    blurRectCount * (downsample_count + 1),
                    shape.rectCount() * 6,
                    data.paint.screen_projection_matrix,
                    windowRect.topLeft());
        glDisable(GL_BLEND);
    }

    vbo->unbindArrays();
}

void BlurEffect::upsample_to_screen(GLVertexBuffer* vbo,
                                    int vboStart,
                                    int blurRectCount,
                                    const QMatrix4x4& screenProjection)
{
    render_targets[1].texture->bind();

    shader->bind(BlurShader::UpSampleType);
    shader->setTargetTextureSize(render_targets[0].texture->size() * effects->renderTargetScale());

    shader->setOffset(offset);
    shader->setModelViewProjectionMatrix(screenProjection);

    // Render to the screen
    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    shader->unbind();
}

void BlurEffect::apply_noise(GLVertexBuffer* vbo,
                             int vboStart,
                             int blurRectCount,
                             const QMatrix4x4& screenProjection,
                             QPoint windowPosition)
{
    if (noise_texture.isNull()) {
        generate_noise_texture();
    }

    shader->bind(BlurShader::NoiseSampleType);
    shader->setTargetTextureSize(render_targets[0].texture->size() * effects->renderTargetScale());
    shader->setNoiseTextureSize(noise_texture.size() * effects->renderTargetScale());
    shader->setTexturePosition(windowPosition * effects->renderTargetScale());

    noise_texture.bind();

    shader->setOffset(offset);
    shader->setModelViewProjectionMatrix(screenProjection);

    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    shader->unbind();
}

void BlurEffect::downsample_texture(GLVertexBuffer* vbo, int blurRectCount)
{
    QMatrix4x4 modelViewProjectionMatrix;

    shader->bind(BlurShader::DownSampleType);
    shader->setOffset(offset);

    for (int i = 1; i <= downsample_count; i++) {
        modelViewProjectionMatrix.setToIdentity();
        modelViewProjectionMatrix.ortho(0,
                                        render_targets[i].texture->width(),
                                        render_targets[i].texture->height(),
                                        0,
                                        0,
                                        65535);

        shader->setModelViewProjectionMatrix(modelViewProjectionMatrix);
        shader->setTargetTextureSize(render_targets[i].texture->size());

        // Copy the image from this texture
        render_targets[i - 1].texture->bind();

        vbo->draw(GL_TRIANGLES, blurRectCount * i, blurRectCount);
        GLFramebuffer::popRenderTarget();
    }

    shader->unbind();
}

void BlurEffect::upsample_texture(GLVertexBuffer* vbo, int blurRectCount)
{
    QMatrix4x4 modelViewProjectionMatrix;

    shader->bind(BlurShader::UpSampleType);
    shader->setOffset(offset);

    for (int i = downsample_count - 1; i >= 1; i--) {
        modelViewProjectionMatrix.setToIdentity();
        modelViewProjectionMatrix.ortho(0,
                                        render_targets[i].texture->width(),
                                        render_targets[i].texture->height(),
                                        0,
                                        0,
                                        65535);

        shader->setModelViewProjectionMatrix(modelViewProjectionMatrix);
        shader->setTargetTextureSize(render_targets[i].texture->size());

        // Copy the image from this texture
        render_targets[i + 1].texture->bind();

        vbo->draw(GL_TRIANGLES, blurRectCount * i, blurRectCount);
        GLFramebuffer::popRenderTarget();
    }

    shader->unbind();
}

void BlurEffect::copy_screen_sample_texture(GLVertexBuffer* vbo,
                                            int blurRectCount,
                                            QRegion blurShape,
                                            const QMatrix4x4& screenProjection)
{
    shader->bind(BlurShader::CopySampleType);

    shader->setModelViewProjectionMatrix(screenProjection);
    shader->setTargetTextureSize(effects->virtualScreenSize());

    /*
     * This '1' sized adjustment is necessary do avoid windows affecting the blur that are
     * right next to this window.
     */
    shader->setBlurRect(blurShape.boundingRect().adjusted(1, 1, -1, -1),
                        effects->virtualScreenSize());
    render_targets.back().texture->bind();

    vbo->draw(GL_TRIANGLES, 0, blurRectCount);
    GLFramebuffer::popRenderTarget();

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
