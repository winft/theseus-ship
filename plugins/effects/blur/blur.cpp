/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

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
        effect.blurRegions[update.base.window] = update.value;
    } else {
        effect.blurRegions.remove(update.base.window);
    }
}

BlurEffect::BlurEffect()
{
    initConfig<BlurConfig>();
    m_shader = new BlurShader(this);

    initBlurStrengthValues();
    reconfigure(ReconfigureAll);

    if (m_shader && m_shader->isValid() && m_renderTargetsValid) {
        auto& blur_integration = effects->get_blur_integration();
        auto update = [this](auto&& data) { update_function(*this, data); };
        blur_integration.add(*this, update);
    }
}

BlurEffect::~BlurEffect()
{
    m_renderTargets.clear();
    m_renderTextures.clear();
}

void BlurEffect::reset()
{
    effects->makeOpenGLContextCurrent();
    updateTexture();
    effects->doneOpenGLContextCurrent();
}

bool BlurEffect::renderTargetsValid() const
{
    return !m_renderTargets.empty()
        && std::all_of(m_renderTargets.cbegin(), m_renderTargets.cend(), [](auto&& target) {
               return target->valid();
           });
    ;
}

void BlurEffect::updateTexture()
{
    m_renderTargets.clear();
    m_renderTextures.clear();

    /* Reserve memory for:
     *  - The original sized texture (1)
     *  - The downsized textures (m_downSampleIterations)
     *  - The helper texture (1)
     */
    m_renderTargets.reserve(m_downSampleIterations + 2);
    m_renderTextures.reserve(m_downSampleIterations + 2);

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

    for (int i = 0; i <= m_downSampleIterations; i++) {
        m_renderTextures.emplace_back(
            std::make_unique<GLTexture>(textureFormat, effects->virtualScreenSize() / (1 << i)));
        m_renderTextures.back()->setFilter(GL_LINEAR);
        m_renderTextures.back()->setWrapMode(GL_CLAMP_TO_EDGE);

        m_renderTargets.emplace_back(
            std::make_unique<GLFramebuffer>(m_renderTextures.back().get()));
    }

    // This last set is used as a temporary helper texture
    m_renderTextures.emplace_back(
        std::make_unique<GLTexture>(textureFormat, effects->virtualScreenSize()));
    m_renderTextures.back()->setFilter(GL_LINEAR);
    m_renderTextures.back()->setWrapMode(GL_CLAMP_TO_EDGE);

    m_renderTargets.emplace_back(std::make_unique<GLFramebuffer>(m_renderTextures.back().get()));

    m_renderTargetsValid = renderTargetsValid();

    // Prepare the stack for the rendering
    m_renderTargetStack.clear();
    m_renderTargetStack.reserve(m_downSampleIterations * 2);

    // Upsample
    for (int i = 1; i < m_downSampleIterations; i++) {
        m_renderTargetStack.push(m_renderTargets.at(i).get());
    }

    // Downsample
    for (int i = m_downSampleIterations; i > 0; i--) {
        m_renderTargetStack.push(m_renderTargets.at(i).get());
    }

    // Copysample
    m_renderTargetStack.push(m_renderTargets.front().get());

    // Invalidate noise texture
    m_noiseTexture = {};
}

void BlurEffect::initBlurStrengthValues()
{
    // This function creates an array of blur strength values that are evenly distributed

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
     * The minOffset variable is the minimum offset value for an iteration before we
     * get blocky artifacts because of the downsampling.
     *
     * The maxOffset value is the maximum offset value for an iteration before we
     * get diagonal line artifacts because of the nature of the dual kawase blur algorithm.
     *
     * The expandSize value is the minimum value for an iteration before we reach the end
     * of a texture in the shader and sample outside of the area that was copied into the
     * texture from the screen.
     */

    // {minOffset, maxOffset, expandSize}
    blurOffsets.append({1.0, 2.0, 10});  // Down sample size / 2
    blurOffsets.append({2.0, 3.0, 20});  // Down sample size / 4
    blurOffsets.append({2.0, 5.0, 50});  // Down sample size / 8
    blurOffsets.append({3.0, 8.0, 150}); // Down sample size / 16
    // blurOffsets.append({5.0, 10.0, 400}); // Down sample size / 32
    // blurOffsets.append({7.0, ?.0});       // Down sample size / 64

    float offsetSum = 0;

    for (int i = 0; i < blurOffsets.size(); i++) {
        offsetSum += blurOffsets[i].maxOffset - blurOffsets[i].minOffset;
    }

    for (int i = 0; i < blurOffsets.size(); i++) {
        int iterationNumber = std::ceil((blurOffsets[i].maxOffset - blurOffsets[i].minOffset)
                                        / offsetSum * numOfBlurSteps);
        remainingSteps -= iterationNumber;

        if (remainingSteps < 0) {
            iterationNumber += remainingSteps;
        }

        float offsetDifference = blurOffsets[i].maxOffset - blurOffsets[i].minOffset;

        for (int j = 1; j <= iterationNumber; j++) {
            // {iteration, offset}
            blurStrengthValues.append(
                {i + 1, blurOffsets[i].minOffset + (offsetDifference / iterationNumber) * j});
        }
    }
}

void BlurEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    BlurConfig::self()->read();

    int blurStrength = BlurConfig::blurStrength() - 1;
    m_downSampleIterations = blurStrengthValues[blurStrength].iteration;
    m_offset = blurStrengthValues[blurStrength].offset;
    m_expandSize = blurOffsets[m_downSampleIterations - 1].expandSize;
    m_noiseStrength = BlurConfig::noiseStrength();

    m_scalingFactor = qMax(1.0, QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0);

    updateTexture();

    if (!m_shader || !m_shader->isValid()) {
        effects->get_blur_integration().remove(*this);
    }

    // Update all windows for the blur to take effect
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

bool BlurEffect::decorationSupportsBlurBehind(const EffectWindow* w) const
{
    return w->decoration() && !w->decoration()->blurRegion().isNull();
}

QRegion BlurEffect::decorationBlurRegion(const EffectWindow* w) const
{
    if (!decorationSupportsBlurBehind(w)) {
        return QRegion();
    }

    QRegion decorationRegion = QRegion(w->decoration()->rect()) - w->decorationInnerRect();

    // we return only blurred regions that belong to decoration region
    return decorationRegion.intersected(w->decoration()->blurRegion());
}

QRect BlurEffect::expand(const QRect& rect) const
{
    return rect.adjusted(-m_expandSize, -m_expandSize, m_expandSize, m_expandSize);
}

QRegion BlurEffect::expand(const QRegion& region) const
{
    QRegion expanded;

    for (const QRect& rect : region) {
        expanded += expand(rect);
    }

    return expanded;
}

QRegion BlurEffect::blurRegion(const EffectWindow* w) const
{
    QRegion region;

    if (auto it = blurRegions.find(w); it != blurRegions.end()) {
        const QRegion& appRegion = *it;
        if (!appRegion.isEmpty()) {
            if (w->decorationHasAlpha() && decorationSupportsBlurBehind(w)) {
                region = decorationBlurRegion(w);
            }
            region |= appRegion.translated(w->contentsRect().topLeft()) & w->decorationInnerRect();
        } else {
            // An empty region means that the blur effect should be enabled
            // for the whole window.
            region = w->rect();
        }
    } else if (w->decorationHasAlpha() && decorationSupportsBlurBehind(w)) {
        // If the client hasn't specified a blur region, we'll only enable
        // the effect behind the decoration.
        region = decorationBlurRegion(w);
    }

    return region;
}

void BlurEffect::uploadRegion(QVector2D*& map,
                              QRegion const& region,
                              int const downSampleIterations)
{
    for (int i = 0; i <= downSampleIterations; i++) {
        auto const divisionRatio = (1 << i);

        for (const QRect& r : region) {
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

void BlurEffect::uploadGeometry(GLVertexBuffer* vbo,
                                const QRegion& blurRegion,
                                const QRegion& windowRegion)
{
    auto const vertexCount
        = ((blurRegion.rectCount() * (m_downSampleIterations + 1)) + windowRegion.rectCount()) * 6;
    if (!vertexCount) {
        return;
    }

    auto map = static_cast<QVector2D*>(vbo->map(vertexCount * sizeof(QVector2D)));

    uploadRegion(map, blurRegion, m_downSampleIterations);
    uploadRegion(map, windowRegion, 0);

    vbo->unmap();

    GLVertexAttrib const layout[] = {{VA_Position, 2, GL_FLOAT, 0}, {VA_TexCoord, 2, GL_FLOAT, 0}};

    vbo->setAttribLayout(layout, 2, sizeof(QVector2D));
}

void BlurEffect::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    m_paintedArea = QRegion();
    m_currentBlur = QRegion();

    effects->prePaintScreen(data, presentTime);
}

void BlurEffect::prePaintWindow(EffectWindow* w,
                                WindowPrePaintData& data,
                                std::chrono::milliseconds presentTime)
{
    // this effect relies on prePaintWindow being called in the bottom to top order

    effects->prePaintWindow(w, data, presentTime);

    if (!m_shader || !m_shader->isValid()) {
        return;
    }

    const QRegion oldClip = data.clip;
    if (data.clip.intersects(m_currentBlur)) {
        // to blur an area partially we have to shrink the opaque area of a window
        QRegion newClip;
        for (const QRect& rect : data.clip) {
            newClip |= rect.adjusted(m_expandSize, m_expandSize, -m_expandSize, -m_expandSize);
        }
        data.clip = newClip;

        // we don't have to blur a region we don't see
        m_currentBlur -= newClip;
    }

    // if we have to paint a non-opaque part of this window that intersects with the
    // currently blurred region we have to redraw the whole region
    if ((data.paint - oldClip).intersects(m_currentBlur)) {
        data.paint |= m_currentBlur;
    }

    // in case this window has regions to be blurred
    const QRect screen = effects->virtualScreenGeometry();
    const QRegion blurArea = blurRegion(w).translated(w->pos()) & screen;
    const QRegion expandedBlur = (w->isDock() ? blurArea : expand(blurArea)) & screen;

    // if this window or a window underneath the blurred area is painted again we have to
    // blur everything
    if (m_paintedArea.intersects(expandedBlur) || data.paint.intersects(blurArea)) {
        data.paint |= expandedBlur;
        // we have to check again whether we do not damage a blurred area
        // of a window
        if (expandedBlur.intersects(m_currentBlur)) {
            data.paint |= m_currentBlur;
        }
    }

    m_currentBlur |= expandedBlur;

    m_paintedArea -= data.clip;
    m_paintedArea |= data.paint;
}

bool BlurEffect::shouldBlur(const EffectWindow* w, int mask, const WindowPaintData& data) const
{
    if (!m_renderTargetsValid || !m_shader || !m_shader->isValid()) {
        return false;
    }
    if (effects->activeFullScreenEffect() && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }
    if (w->isDesktop()) {
        return false;
    }

    auto const scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    auto const translated = data.xTranslation() || data.yTranslation();

    if ((scaled || (translated || (mask & PAINT_WINDOW_TRANSFORMED)))
        && !w->data(WindowForceBlurRole).toBool()) {
        return false;
    }

    return true;
}

void BlurEffect::drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data)
{
    if (!shouldBlur(w, mask, data)) {
        effects->drawWindow(w, mask, region, data);
        return;
    }

    auto const screen = effects->renderTargetRect();
    auto shape = blurRegion(w).translated(w->pos());

    // let's do the evil parts - someone wants to blur behind a transformed window
    auto const translated = data.xTranslation() || data.yTranslation();
    auto const scaled = data.xScale() != 1 || data.yScale() != 1;

    if (scaled) {
        auto pt = shape.boundingRect().topLeft();
        QRegion scaledShape;

        for (auto r : shape) {
            r.moveTo(pt.x() + (r.x() - pt.x()) * data.xScale() + data.xTranslation(),
                     pt.y() + (r.y() - pt.y()) * data.yScale() + data.yTranslation());
            r.setWidth(r.width() * data.xScale());
            r.setHeight(r.height() * data.yScale());
            scaledShape |= r;
        }

        shape = scaledShape;
    } else if (translated) {
        // Only translated, not scaled
        shape = shape.translated(data.xTranslation(), data.yTranslation());
    }

    auto modal = w->transientFor();
    auto const transientForIsDock = (modal ? modal->isDock() : false);

    doBlur(shape & region,
           screen,
           data.opacity(),
           data.screenProjectionMatrix(),
           w->isDock() || transientForIsDock,
           w->frameGeometry());

    // Draw the window over the blurred area
    effects->drawWindow(w, mask, region, data);
}

void BlurEffect::generateNoiseTexture()
{
    if (m_noiseStrength == 0) {
        return;
    }

    // Init randomness based on time
    std::srand(static_cast<uint>(QTime::currentTime().msec()));

    QImage noiseImage(QSize(256, 256), QImage::Format_Grayscale8);

    for (int y = 0; y < noiseImage.height(); y++) {
        uint8_t* noiseImageLine = static_cast<uint8_t*>(noiseImage.scanLine(y));

        for (int x = 0; x < noiseImage.width(); x++) {
            noiseImageLine[x] = std::rand() % m_noiseStrength;
        }
    }

    // The noise texture looks distorted when not scaled with integer
    noiseImage = noiseImage.scaled(noiseImage.size() * m_scalingFactor);

    m_noiseTexture = GLTexture(noiseImage);
    m_noiseTexture.setFilter(GL_NEAREST);
    m_noiseTexture.setWrapMode(GL_REPEAT);
}

void BlurEffect::doBlur(const QRegion& shape,
                        const QRect& screen,
                        const float opacity,
                        const QMatrix4x4& screenProjection,
                        bool isDock,
                        QRect windowRect)
{
    if (shape.isEmpty()) {
        return;
    }

    // Blur would not render correctly on a secondary monitor because of wrong coordinates
    // BUG: 393723
    auto const xTranslate = -screen.x();
    auto const yTranslate = effects->virtualScreenSize().height() - screen.height() - screen.y();

    auto const expandedBlurRegion = expand(shape) & expand(screen);
    auto const useSRGB = m_renderTextures.front()->internalFormat() == GL_SRGB8_ALPHA8;

    // Upload geometry for the down and upsample iterations
    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();

    uploadGeometry(vbo, expandedBlurRegion.translated(xTranslate, yTranslate), shape);
    vbo->bindArrays();

    const QRect sourceRect = expandedBlurRegion.boundingRect() & screen;
    const QRect destRect = sourceRect.translated(xTranslate, yTranslate);
    int blurRectCount = expandedBlurRegion.rectCount() * 6;

    /*
     * If the window is a dock or panel we avoid the "extended blur" effect.
     * Extended blur is when windows that are not under the blurred area affect
     * the final blur result.
     * We want to avoid this on panels, because it looks really weird and ugly
     * when maximized windows or windows near the panel affect the dock blur.
     */
    if (isDock) {
        m_renderTargets.back()->blitFromFramebuffer(effects->mapToRenderTarget(sourceRect),
                                                    destRect);
        GLFramebuffer::pushRenderTargets(m_renderTargetStack);

        if (useSRGB) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }

        const QRect screenRect = effects->virtualScreenGeometry();
        QMatrix4x4 mvp;
        mvp.ortho(0, screenRect.width(), screenRect.height(), 0, 0, 65535);
        copyScreenSampleTexture(vbo, blurRectCount, shape.translated(xTranslate, yTranslate), mvp);
    } else {
        m_renderTargets.front()->blitFromFramebuffer(effects->mapToRenderTarget(sourceRect),
                                                     destRect);
        GLFramebuffer::pushRenderTargets(m_renderTargetStack);

        if (useSRGB) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }

        // Remove the m_renderTargets[0] from the top of the stack that we will not use
        GLFramebuffer::popRenderTarget();
    }

    downSampleTexture(vbo, blurRectCount);
    upSampleTexture(vbo, blurRectCount);

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

    upscaleRenderToScreen(vbo,
                          blurRectCount * (m_downSampleIterations + 1),
                          shape.rectCount() * 6,
                          screenProjection,
                          windowRect.topLeft());

    if (useSRGB) {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }

    if (opacity < 1.0) {
        glDisable(GL_BLEND);
    }

    if (m_noiseStrength > 0) {
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
        applyNoise(vbo,
                   blurRectCount * (m_downSampleIterations + 1),
                   shape.rectCount() * 6,
                   screenProjection,
                   windowRect.topLeft());
        glDisable(GL_BLEND);
    }

    vbo->unbindArrays();
}

void BlurEffect::upscaleRenderToScreen(GLVertexBuffer* vbo,
                                       int vboStart,
                                       int blurRectCount,
                                       const QMatrix4x4& screenProjection,
                                       QPoint /*windowPosition*/)
{
    m_renderTextures[1]->bind();

    m_shader->bind(BlurShader::UpSampleType);
    m_shader->setTargetTextureSize(m_renderTextures[0]->size() * effects->renderTargetScale());

    m_shader->setOffset(m_offset);
    m_shader->setModelViewProjectionMatrix(screenProjection);

    // Render to the screen
    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    m_shader->unbind();
}

void BlurEffect::applyNoise(GLVertexBuffer* vbo,
                            int vboStart,
                            int blurRectCount,
                            const QMatrix4x4& screenProjection,
                            QPoint windowPosition)
{
    if (m_noiseTexture.isNull()) {
        generateNoiseTexture();
    }

    m_shader->bind(BlurShader::NoiseSampleType);
    m_shader->setTargetTextureSize(m_renderTextures[0]->size() * effects->renderTargetScale());
    m_shader->setNoiseTextureSize(m_noiseTexture.size() * effects->renderTargetScale());
    m_shader->setTexturePosition(windowPosition * effects->renderTargetScale());

    m_noiseTexture.bind();

    m_shader->setOffset(m_offset);
    m_shader->setModelViewProjectionMatrix(screenProjection);

    vbo->draw(GL_TRIANGLES, vboStart, blurRectCount);
    m_shader->unbind();
}

void BlurEffect::downSampleTexture(GLVertexBuffer* vbo, int blurRectCount)
{
    QMatrix4x4 modelViewProjectionMatrix;

    m_shader->bind(BlurShader::DownSampleType);
    m_shader->setOffset(m_offset);

    for (int i = 1; i <= m_downSampleIterations; i++) {
        modelViewProjectionMatrix.setToIdentity();
        modelViewProjectionMatrix.ortho(
            0, m_renderTextures[i]->width(), m_renderTextures[i]->height(), 0, 0, 65535);

        m_shader->setModelViewProjectionMatrix(modelViewProjectionMatrix);
        m_shader->setTargetTextureSize(m_renderTextures[i]->size());

        // Copy the image from this texture
        m_renderTextures[i - 1]->bind();

        vbo->draw(GL_TRIANGLES, blurRectCount * i, blurRectCount);
        GLFramebuffer::popRenderTarget();
    }

    m_shader->unbind();
}

void BlurEffect::upSampleTexture(GLVertexBuffer* vbo, int blurRectCount)
{
    QMatrix4x4 modelViewProjectionMatrix;

    m_shader->bind(BlurShader::UpSampleType);
    m_shader->setOffset(m_offset);

    for (int i = m_downSampleIterations - 1; i >= 1; i--) {
        modelViewProjectionMatrix.setToIdentity();
        modelViewProjectionMatrix.ortho(
            0, m_renderTextures[i]->width(), m_renderTextures[i]->height(), 0, 0, 65535);

        m_shader->setModelViewProjectionMatrix(modelViewProjectionMatrix);
        m_shader->setTargetTextureSize(m_renderTextures[i]->size());

        // Copy the image from this texture
        m_renderTextures[i + 1]->bind();

        vbo->draw(GL_TRIANGLES, blurRectCount * i, blurRectCount);
        GLFramebuffer::popRenderTarget();
    }

    m_shader->unbind();
}

void BlurEffect::copyScreenSampleTexture(GLVertexBuffer* vbo,
                                         int blurRectCount,
                                         QRegion blurShape,
                                         const QMatrix4x4& screenProjection)
{
    m_shader->bind(BlurShader::CopySampleType);

    m_shader->setModelViewProjectionMatrix(screenProjection);
    m_shader->setTargetTextureSize(effects->virtualScreenSize());

    /*
     * This '1' sized adjustment is necessary do avoid windows affecting the blur that are
     * right next to this window.
     */
    m_shader->setBlurRect(blurShape.boundingRect().adjusted(1, 1, -1, -1),
                          effects->virtualScreenSize());
    m_renderTextures.back()->bind();

    vbo->draw(GL_TRIANGLES, 0, blurRectCount);
    GLFramebuffer::popRenderTarget();

    m_shader->unbind();
}

bool BlurEffect::isActive() const
{
    return !effects->isScreenLocked();
}

} // namespace KWin
