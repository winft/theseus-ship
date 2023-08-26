/*
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2010 Nokia Corporation and /or its subsidiary(-ies)
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screenshot.h"
#include "screenshotdbusinterface2.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/gl/interface/platform.h>
#include <render/gl/interface/utils.h>

#include <QPainter>

Q_LOGGING_CATEGORY(KWIN_SCREENSHOT, "kwin_effect_screenshot", QtWarningMsg)

namespace KWin
{

struct ScreenShotWindowData {
    QPromise<QImage> promise;
    ScreenShotFlags flags;
    EffectWindow* window = nullptr;
};

struct ScreenShotAreaData {
    QPromise<QImage> promise;
    ScreenShotFlags flags;
    QRect area;
    QImage result;
    QList<EffectScreen*> screens;
};

struct ScreenShotScreenData {
    QPromise<QImage> promise;
    ScreenShotFlags flags;
    EffectScreen* screen = nullptr;
};

static void
convertFromGLImage(QImage& img, int w, int h, QMatrix4x4 const& renderTargetTransformation)
{
    // from QtOpenGL/qgl.cpp
    // SPDX-FileCopyrightText: 2010 Nokia Corporation and /or its subsidiary(-ies)
    // see https://github.com/qt/qtbase/blob/dev/src/opengl/qgl.cpp
    if (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
        // OpenGL gives RGBA; Qt wants ARGB
        uint* p = reinterpret_cast<uint*>(img.bits());
        uint* end = p + w * h;
        while (p < end) {
            uint a = *p << 24;
            *p = (*p >> 8) | a;
            p++;
        }
    } else {
        // OpenGL gives ABGR (i.e. RGBA backwards); Qt wants ARGB
        for (int y = 0; y < h; y++) {
            uint* q = reinterpret_cast<uint*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const uint pixel = *q;
                *q = ((pixel << 16) & 0xff0000) | ((pixel >> 16) & 0xff) | (pixel & 0xff00ff00);

                q++;
            }
        }
    }

    // OpenGL textures are flipped vs QImage
    QMatrix4x4 matrix;
    matrix.scale(1, -1);

    // apply render target transformation
    matrix *= renderTargetTransformation.inverted();
    img = img.transformed(matrix.toTransform());
}

bool ScreenShotEffect::supported()
{
    return effects->isOpenGLCompositing() && GLFramebuffer::supported();
}

ScreenShotEffect::ScreenShotEffect()
    : m_dbusInterface2(new ScreenShotDBusInterface2(this))
{
    connect(effects, &EffectsHandler::screenAdded, this, &ScreenShotEffect::handleScreenAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &ScreenShotEffect::handleScreenRemoved);
    connect(effects, &EffectsHandler::windowClosed, this, &ScreenShotEffect::handleWindowClosed);
}

ScreenShotEffect::~ScreenShotEffect()
{
    cancelWindowScreenShots();
    cancelAreaScreenShots();
    cancelScreenScreenShots();
}

QFuture<QImage> ScreenShotEffect::scheduleScreenShot(EffectScreen* screen, ScreenShotFlags flags)
{
    for (const ScreenShotScreenData& data : m_screenScreenShots) {
        if (data.screen == screen && data.flags == flags) {
            return data.promise.future();
        }
    }

    ScreenShotScreenData data;
    data.screen = screen;
    data.flags = flags;

    data.promise.start();
    QFuture<QImage> future = data.promise.future();

    m_screenScreenShots.push_back(std::move(data));
    effects->addRepaint(screen->geometry());

    return future;
}

QFuture<QImage> ScreenShotEffect::scheduleScreenShot(const QRect& area, ScreenShotFlags flags)
{
    for (const ScreenShotAreaData& data : m_areaScreenShots) {
        if (data.area == area && data.flags == flags) {
            return data.promise.future();
        }
    }

    ScreenShotAreaData data;
    data.area = area;
    data.flags = flags;

    auto const screens = effects->screens();
    for (auto screen : screens) {
        if (screen->geometry().intersects(area)) {
            data.screens.append(screen);
        }
    }

    auto devicePixelRatio = 1.;
    if (flags & ScreenShotNativeResolution) {
        for (auto const screen : qAsConst(data.screens)) {
            if (screen->devicePixelRatio() > devicePixelRatio) {
                devicePixelRatio = screen->devicePixelRatio();
            }
        }
    }

    data.result = QImage(area.size() * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
    data.result.fill(Qt::transparent);
    data.result.setDevicePixelRatio(devicePixelRatio);

    data.promise.start();
    QFuture<QImage> future = data.promise.future();

    m_areaScreenShots.push_back(std::move(data));
    effects->addRepaint(area);

    return future;
}

QFuture<QImage> ScreenShotEffect::scheduleScreenShot(EffectWindow* window, ScreenShotFlags flags)
{
    for (const ScreenShotWindowData& data : m_windowScreenShots) {
        if (data.window == window && data.flags == flags) {
            return data.promise.future();
        }
    }

    ScreenShotWindowData data;
    data.window = window;
    data.flags = flags;

    data.promise.start();
    QFuture<QImage> future = data.promise.future();

    m_windowScreenShots.push_back(std::move(data));
    window->addRepaintFull();

    return future;
}

void ScreenShotEffect::cancelWindowScreenShots()
{
    m_windowScreenShots.clear();
}

void ScreenShotEffect::cancelAreaScreenShots()
{
    m_areaScreenShots.clear();
}

void ScreenShotEffect::cancelScreenScreenShots()
{
    m_screenScreenShots.clear();
}

void ScreenShotEffect::paintScreen(effect::screen_paint_data& data)
{
    m_paintedScreen = data.screen;
    effects->paintScreen(data);

    for (auto& win_data : m_windowScreenShots) {
        takeScreenShot(&win_data);
    }
    m_windowScreenShots.clear();

    for (int i = m_areaScreenShots.size() - 1; i >= 0; --i) {
        if (takeScreenShot(data.render, &m_areaScreenShots[i])) {
            m_areaScreenShots.erase(m_areaScreenShots.begin() + i);
        }
    }

    for (int i = m_screenScreenShots.size() - 1; i >= 0; --i) {
        if (takeScreenShot(data.render, &m_screenScreenShots[i])) {
            m_screenScreenShots.erase(m_screenScreenShots.begin() + i);
        }
    }
}

void ScreenShotEffect::takeScreenShot(ScreenShotWindowData* screenshot)
{
    auto window = screenshot->window;
    auto geometry = window->expandedGeometry();
    auto devicePixelRatio = 1.;

    if (window->hasDecoration() && !(screenshot->flags & ScreenShotIncludeDecoration)) {
        geometry = window->clientGeometry();
    }
    if (screenshot->flags & ScreenShotNativeResolution) {
        if (auto const screen = window->screen()) {
            devicePixelRatio = screen->devicePixelRatio();
        }
    }

    auto validTarget = true;
    std::unique_ptr<GLTexture> offscreenTexture;
    std::unique_ptr<GLFramebuffer> fbo;

    if (effects->isOpenGLCompositing()) {
        offscreenTexture.reset(new GLTexture(GL_RGBA8, geometry.size() * devicePixelRatio));
        offscreenTexture->setFilter(GL_LINEAR);
        offscreenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        fbo = std::make_unique<GLFramebuffer>(offscreenTexture.get());
        validTarget = fbo->valid();
    }

    if (!validTarget) {
        return;
    }

    QImage img;
    if (effects->isOpenGLCompositing()) {
        QMatrix4x4 projection;
        projection.ortho(QRect{{}, fbo->size()});
        GLFramebuffer::pushRenderTarget(fbo.get());

        effect::window_paint_data win_data{
            *window,
            {
                // render window into offscreen texture
                .mask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT,
                .region = infiniteRegion(),
                .geo
                = {.translation
                   = {static_cast<float>(-geometry.x()), static_cast<float>(-geometry.y()), 0.}},
            },
            {.projection = projection},
        };

        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.0, 0.0, 0.0, 1.0);

        effects->drawWindow(win_data);

        // copy content from framebuffer into image
        img = QImage(offscreenTexture->size(), QImage::Format_ARGB32);
        img.setDevicePixelRatio(devicePixelRatio);
        glReadnPixels(0,
                      0,
                      img.width(),
                      img.height(),
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      img.sizeInBytes(),
                      static_cast<GLvoid*>(img.bits()));
        GLFramebuffer::popRenderTarget();
        convertFromGLImage(img, img.width(), img.height(), projection);
        img = img.mirrored();
    }

    if (screenshot->flags & ScreenShotIncludeCursor) {
        grabPointerImage(img, geometry.x(), geometry.y());
    }

    screenshot->promise.addResult(img);
    screenshot->promise.finish();
}

bool ScreenShotEffect::takeScreenShot(effect::render_data const& render_data,
                                      ScreenShotAreaData* screenshot)
{
    if (!m_paintedScreen) {
        // On X11, all screens are painted simultaneously and there is no native HiDPI support.
        auto snapshot = blitScreenshot(render_data, screenshot->area);
        if (screenshot->flags & ScreenShotIncludeCursor) {
            grabPointerImage(snapshot, screenshot->area.x(), screenshot->area.y());
        }
        screenshot->promise.addResult(snapshot);
        screenshot->promise.finish();
        return true;
    }

    if (!screenshot->screens.contains(m_paintedScreen)) {
        return false;
    }

    screenshot->screens.removeOne(m_paintedScreen);

    auto const sourceRect = screenshot->area & m_paintedScreen->geometry();
    auto sourceDevicePixelRatio = 1.0;
    if (screenshot->flags & ScreenShotNativeResolution) {
        sourceDevicePixelRatio = m_paintedScreen->devicePixelRatio();
    }

    auto const snapshot = blitScreenshot(render_data, sourceRect, sourceDevicePixelRatio);
    QRect const nativeArea(screenshot->area.topLeft(),
                           screenshot->area.size() * screenshot->result.devicePixelRatio());

    QPainter painter(&screenshot->result);
    painter.setWindow(nativeArea);
    painter.drawImage(sourceRect, snapshot);
    painter.end();

    if (screenshot->screens.isEmpty()) {
        if (screenshot->flags & ScreenShotIncludeCursor) {
            grabPointerImage(screenshot->result, screenshot->area.x(), screenshot->area.y());
        }
        screenshot->promise.addResult(screenshot->result);
        screenshot->promise.finish();
        return true;
    }

    return false;
}

bool ScreenShotEffect::takeScreenShot(effect::render_data const& render_data,
                                      ScreenShotScreenData* screenshot)
{
    if (m_paintedScreen && screenshot->screen != m_paintedScreen) {
        return false;
    }

    auto devicePixelRatio = 1.;
    if (screenshot->flags & ScreenShotNativeResolution) {
        devicePixelRatio = screenshot->screen->devicePixelRatio();
    }

    auto snapshot = blitScreenshot(render_data, screenshot->screen->geometry(), devicePixelRatio);
    if (screenshot->flags & ScreenShotIncludeCursor) {
        const int xOffset = screenshot->screen->geometry().x();
        const int yOffset = screenshot->screen->geometry().y();
        grabPointerImage(snapshot, xOffset, yOffset);
    }

    screenshot->promise.addResult(snapshot);
    screenshot->promise.finish();

    return true;
}

QImage ScreenShotEffect::blitScreenshot(effect::render_data const& render_data,
                                        const QRect& geometry,
                                        qreal devicePixelRatio) const
{
    return effects->blit_from_framebuffer(render_data, geometry, devicePixelRatio);
}

void ScreenShotEffect::grabPointerImage(QImage& snapshot, int xOffset, int yOffset) const
{
    if (effects->isCursorHidden()) {
        return;
    }

    auto const cursor = effects->cursorImage();
    if (cursor.image.isNull()) {
        return;
    }

    QPainter painter(&snapshot);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(effects->cursorPos() - cursor.hot_spot - QPoint(xOffset, yOffset),
                      cursor.image);
}

bool ScreenShotEffect::isActive() const
{
    return (!m_windowScreenShots.empty() || !m_areaScreenShots.empty()
            || !m_screenScreenShots.empty())
        && !effects->isScreenLocked();
}

int ScreenShotEffect::requestedEffectChainPosition() const
{
    return 0;
}

void ScreenShotEffect::handleScreenAdded()
{
    cancelAreaScreenShots();
}

void ScreenShotEffect::handleScreenRemoved(EffectScreen* screen)
{
    cancelAreaScreenShots();

    std::erase_if(m_screenScreenShots,
                  [screen](const auto& screenshot) { return screenshot.screen == screen; });
}

void ScreenShotEffect::handleWindowClosed(EffectWindow* window)
{
    std::erase_if(m_windowScreenShots,
                  [window](const auto& screenshot) { return screenshot.window == window; });
}

} // namespace KWin
