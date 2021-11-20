/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2023 Sergio Blanco <seral79@gmail.com>

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
#include "thumbnail_item.h"

#include "render/compositor_qobject.h"
#include "scripting/scripting_logging.h"
#include "scripting/singleton_interface.h"
#include "scripting/space.h"
#include "win/singleton_interface.h"

#include <kwineffects/effects_handler.h>
#include <kwingl/texture.h>
#include <kwingl/utils.h>

#include <QQuickWindow>
#include <QRunnable>
#include <QSGImageNode>
#include <QSGTextureProvider>

namespace KWin::render
{
class ThumbnailTextureProvider : public QSGTextureProvider
{
public:
    explicit ThumbnailTextureProvider(QQuickWindow* window);

    QSGTexture* texture() const override;
    void setTexture(QSharedPointer<GLTexture> const& nativeTexture);
    void setTexture(QSGTexture* texture);

private:
    QQuickWindow* m_window;
    QSharedPointer<GLTexture> m_nativeTexture;
    QScopedPointer<QSGTexture> m_texture;
};

ThumbnailTextureProvider::ThumbnailTextureProvider(QQuickWindow* window)
    : m_window(window)
{
}

QSGTexture* ThumbnailTextureProvider::texture() const
{
    return m_texture.data();
}

void ThumbnailTextureProvider::setTexture(QSharedPointer<GLTexture> const& nativeTexture)
{
    if (m_nativeTexture != nativeTexture) {
        auto const textureId = nativeTexture->texture();
        m_nativeTexture = nativeTexture;
        m_texture.reset(
            m_window->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                    &textureId,
                                                    0,
                                                    nativeTexture->size(),
                                                    QQuickWindow::TextureHasAlphaChannel));
        m_texture->setFiltering(QSGTexture::Linear);
        m_texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        m_texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    }

    // The textureChanged signal must be emitted also if only texture data changes.
    Q_EMIT textureChanged();
}

void ThumbnailTextureProvider::setTexture(QSGTexture* texture)
{
    m_nativeTexture = nullptr;
    m_texture.reset(texture);
    Q_EMIT textureChanged();
}

class ThumbnailTextureProviderCleanupJob : public QRunnable
{
public:
    explicit ThumbnailTextureProviderCleanupJob(ThumbnailTextureProvider* provider)
        : m_provider(provider)
    {
    }

    void run() override
    {
        m_provider.reset();
    }

private:
    QScopedPointer<ThumbnailTextureProvider> m_provider;
};

basic_thumbnail_item::basic_thumbnail_item(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    update_render_notifier();

    connect(singleton_interface::compositor,
            &render::compositor_qobject::aboutToToggleCompositing,
            this,
            &basic_thumbnail_item::destroyOffscreenTexture);
    connect(singleton_interface::compositor,
            &render::compositor_qobject::compositingToggled,
            this,
            &basic_thumbnail_item::update_render_notifier);
    connect(this, &QQuickItem::windowChanged, this, &basic_thumbnail_item::update_render_notifier);
}

basic_thumbnail_item::~basic_thumbnail_item()
{
    destroyOffscreenTexture();

    if (m_provider) {
        if (window()) {
            window()->scheduleRenderJob(new ThumbnailTextureProviderCleanupJob(m_provider),
                                        QQuickWindow::AfterSynchronizingStage);
        } else {
            qCCritical(KWIN_SCRIPTING)
                << "Can't destroy thumbnail texture provider because window is null";
        }
    }
}

void basic_thumbnail_item::releaseResources()
{
    if (m_provider) {
        window()->scheduleRenderJob(new ThumbnailTextureProviderCleanupJob(m_provider),
                                    QQuickWindow::AfterSynchronizingStage);
        m_provider = nullptr;
    }
}

bool basic_thumbnail_item::isTextureProvider() const
{
    return true;
}

QSGTextureProvider* basic_thumbnail_item::textureProvider() const
{
    if (QQuickItem::isTextureProvider()) {
        return QQuickItem::textureProvider();
    }
    if (!m_provider) {
        m_provider = new ThumbnailTextureProvider(window());
    }
    return m_provider;
}

void basic_thumbnail_item::update_render_notifier()
{
    disconnect(render_notifier);

    if (!effects) {
        return;
    }

    if (!window()) {
        return;
    }

    if (effects->isOpenGLCompositing()) {
        render_notifier = connect(effects,
                                  &EffectsHandler::frameRendered,
                                  this,
                                  &basic_thumbnail_item::updateOffscreenTexture);
    }
}

QSize basic_thumbnail_item::sourceSize() const
{
    return m_sourceSize;
}

void basic_thumbnail_item::setSourceSize(const QSize& sourceSize)
{
    if (m_sourceSize != sourceSize) {
        m_sourceSize = sourceSize;
        invalidateOffscreenTexture();
        Q_EMIT sourceSizeChanged();
    }
}

void basic_thumbnail_item::destroyOffscreenTexture()
{
    if (!effects || !effects->isOpenGLCompositing()) {
        return;
    }

    if (m_offscreenTexture) {
        effects->makeOpenGLContextCurrent();
        m_offscreenTarget.reset();
        m_offscreenTexture.reset();

        if (m_acquireFence) {
            glDeleteSync(m_acquireFence);
            m_acquireFence = 0;
        }
        effects->doneOpenGLContextCurrent();
    }
}

QSGNode* basic_thumbnail_item::updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData*)
{
    if (effects && !m_offscreenTexture) {
        return oldNode;
    }

    // Wait for rendering commands to the offscreen texture complete if there are any.
    if (m_acquireFence) {
        glClientWaitSync(m_acquireFence, GL_SYNC_FLUSH_COMMANDS_BIT, 5000);
        glDeleteSync(m_acquireFence);
        m_acquireFence = 0;
    }

    if (!m_provider) {
        m_provider = new ThumbnailTextureProvider(window());
    }

    if (m_offscreenTexture) {
        m_provider->setTexture(m_offscreenTexture);
    } else {
        auto const placeholderImage = fallbackImage();
        m_provider->setTexture(window()->createTextureFromImage(placeholderImage));
        m_devicePixelRatio = placeholderImage.devicePixelRatio();
    }

    auto node = static_cast<QSGImageNode*>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        node->setFiltering(QSGTexture::Linear);
    }
    node->setTexture(m_provider->texture());

    if (m_offscreenTexture && m_offscreenTexture->isYInverted()) {
        node->setTextureCoordinatesTransform(QSGImageNode::MirrorVertically);
    } else {
        node->setTextureCoordinatesTransform(QSGImageNode::NoTransform);
    }

    node->setRect(paintedRect());

    return node;
}

void basic_thumbnail_item::setSaturation(qreal saturation)
{
    Q_UNUSED(saturation)
    qCWarning(KWIN_SCRIPTING)
        << "ThumbnailItem.saturation is removed. Use a shader effect to change saturation";
}

void basic_thumbnail_item::setBrightness(qreal brightness)
{
    Q_UNUSED(brightness)
    qCWarning(KWIN_SCRIPTING)
        << "ThumbnailItem.brightness is removed. Use a shader effect to change brightness";
}

void basic_thumbnail_item::setClipTo(QQuickItem* clip)
{
    Q_UNUSED(clip)
    qCWarning(KWIN_SCRIPTING) << "ThumbnailItem.clipTo is removed and it has no replacements";
}

window_thumbnail_item::window_thumbnail_item(QQuickItem* parent)
    : basic_thumbnail_item(parent)
{
}

QUuid window_thumbnail_item::wId() const
{
    return m_wId;
}

scripting::window* find_controlled_window(QUuid const& wId)
{
    auto const windows = scripting::singleton_interface::qt_script_space->clientList();
    for (auto win : windows) {
        if (win->internalId() == wId) {
            return win;
        }
    }
    return nullptr;
}

void window_thumbnail_item::setWId(const QUuid& wId)
{
    if (m_wId == wId) {
        return;
    }
    m_wId = wId;
    if (!m_wId.isNull()) {
        setClient(find_controlled_window(m_wId));
    } else if (m_client) {
        m_client = nullptr;
        updateImplicitSize();
        Q_EMIT clientChanged();
    }
    Q_EMIT wIdChanged();
}

scripting::window* window_thumbnail_item::client() const
{
    return m_client;
}

void window_thumbnail_item::setClient(scripting::window* client)
{
    if (m_client == client) {
        return;
    }
    if (m_client) {
        disconnect(m_client,
                   &scripting::window::frameGeometryChanged,
                   this,
                   &window_thumbnail_item::invalidateOffscreenTexture);
        disconnect(m_client,
                   &scripting::window::damaged,
                   this,
                   &window_thumbnail_item::invalidateOffscreenTexture);
        disconnect(m_client,
                   &scripting::window::frameGeometryChanged,
                   this,
                   &window_thumbnail_item::updateImplicitSize);
    }
    m_client = client;
    if (m_client) {
        connect(m_client,
                &scripting::window::frameGeometryChanged,
                this,
                &window_thumbnail_item::invalidateOffscreenTexture);
        connect(m_client,
                &scripting::window::damaged,
                this,
                &window_thumbnail_item::invalidateOffscreenTexture);
        connect(m_client,
                &scripting::window::frameGeometryChanged,
                this,
                &window_thumbnail_item::updateImplicitSize);
        setWId(m_client->internalId());
    } else {
        setWId(QUuid());
    }
    invalidateOffscreenTexture();
    updateImplicitSize();
    Q_EMIT clientChanged();
}

void window_thumbnail_item::updateImplicitSize()
{
    QSize frameSize;
    if (m_client) {
        frameSize = m_client->frameGeometry().size();
    }
    setImplicitSize(frameSize.width(), frameSize.height());
}

QImage window_thumbnail_item::fallbackImage() const
{
    if (m_client) {
        return m_client->icon().pixmap(window(), boundingRect().size().toSize()).toImage();
    }
    return QImage();
}

static QRectF centeredSize(const QRectF& boundingRect, const QSizeF& size)
{
    auto const scaled = size.scaled(boundingRect.size(), Qt::KeepAspectRatio);
    auto const x = boundingRect.x() + (boundingRect.width() - scaled.width()) / 2;
    auto const y = boundingRect.y() + (boundingRect.height() - scaled.height()) / 2;
    return QRectF(QPointF(x, y), scaled);
}

QRectF window_thumbnail_item::paintedRect() const
{
    if (!m_client) {
        return QRectF();
    }
    if (!m_offscreenTexture) {
        auto const iconSize = m_client->icon().actualSize(window(), boundingRect().size().toSize());
        return centeredSize(boundingRect(), iconSize);
    }

    auto const visibleGeometry = m_client->visibleRect();
    auto const frameGeometry = m_client->frameGeometry();
    auto const scaled
        = QSizeF(frameGeometry.size()).scaled(boundingRect().size(), Qt::KeepAspectRatio);

    auto const xScale = scaled.width() / frameGeometry.width();
    auto const yScale = scaled.height() / frameGeometry.height();

    QRectF paintedRect(boundingRect().x() + (boundingRect().width() - scaled.width()) / 2,
                       boundingRect().y() + (boundingRect().height() - scaled.height()) / 2,
                       visibleGeometry.width() * xScale,
                       visibleGeometry.height() * yScale);

    paintedRect.moveLeft(paintedRect.x() + (visibleGeometry.x() - frameGeometry.x()) * xScale);
    paintedRect.moveTop(paintedRect.y() + (visibleGeometry.y() - frameGeometry.y()) * yScale);

    return paintedRect;
}

void window_thumbnail_item::invalidateOffscreenTexture()
{
    m_dirty = true;
    update();
}

void window_thumbnail_item::updateOffscreenTexture()
{
    if (m_acquireFence || !m_dirty || !m_client) {
        return;
    }
    Q_ASSERT(window());

    auto const geometry = m_client->visibleRect();
    QSize textureSize = geometry.size();
    if (sourceSize().width() > 0) {
        textureSize.setWidth(sourceSize().width());
    }
    if (sourceSize().height() > 0) {
        textureSize.setHeight(sourceSize().height());
    }

    m_devicePixelRatio = window()->devicePixelRatio();
    textureSize *= m_devicePixelRatio;

    if (!m_offscreenTexture || m_offscreenTexture->size() != textureSize) {
        m_offscreenTexture.reset(new GLTexture(GL_RGBA8, textureSize));
        m_offscreenTexture->setFilter(GL_LINEAR);
        m_offscreenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_offscreenTarget.reset(new GLRenderTarget(*m_offscreenTexture));
    }

    GLRenderTarget::pushRenderTarget(m_offscreenTarget.data());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(geometry.x(),
                           geometry.x() + geometry.width(),
                           geometry.y(),
                           geometry.y() + geometry.height(),
                           -1,
                           1);

    auto effectWindow = effects->findWindow(m_wId);
    WindowPaintData data(effectWindow);
    data.setProjectionMatrix(projectionMatrix);

    // The thumbnail must be rendered using kwin's opengl context as VAOs are not
    // shared across contexts. Unfortunately, this also introduces a latency of 1
    // frame, which is not ideal, but it is acceptable for things such as thumbnails.
    auto mask = Effect::PAINT_WINDOW_TRANSFORMED;
    effects->drawWindow(effectWindow, static_cast<int>(mask), infiniteRegion(), data);
    GLRenderTarget::popRenderTarget();

    // The fence is needed to avoid the case where qtquick renderer starts using
    // the texture while all rendering commands to it haven't completed yet.
    m_dirty = false;
    m_acquireFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // We know that the texture has changed, so schedule an item update.
    update();
}

desktop_thumbnail_item::desktop_thumbnail_item(QQuickItem* parent)
    : basic_thumbnail_item(parent)
{
}

int desktop_thumbnail_item::desktop() const
{
    return m_desktop;
}

void desktop_thumbnail_item::setDesktop(int desktop)
{
    desktop = qBound<int>(1, desktop, win::singleton_interface::virtual_desktops->get().size());
    if (m_desktop != desktop) {
        m_desktop = desktop;
        invalidateOffscreenTexture();
        Q_EMIT desktopChanged();
    }
}

QImage desktop_thumbnail_item::fallbackImage() const
{
    return QImage();
}

QRectF desktop_thumbnail_item::paintedRect() const
{
    return centeredSize(boundingRect(), effects->virtualScreenSize());
}

void desktop_thumbnail_item::invalidateOffscreenTexture()
{
    update();
}

void desktop_thumbnail_item::updateOffscreenTexture()
{
    if (m_acquireFence) {
        return;
    }

    auto const geometry = effects->virtualScreenGeometry();
    QSize textureSize = geometry.size();
    if (sourceSize().width() > 0) {
        textureSize.setWidth(sourceSize().width());
    }
    if (sourceSize().height() > 0) {
        textureSize.setHeight(sourceSize().height());
    }

    m_devicePixelRatio = window()->devicePixelRatio();
    textureSize *= m_devicePixelRatio;

    if (!m_offscreenTexture || m_offscreenTexture->size() != textureSize) {
        m_offscreenTexture.reset(new GLTexture(GL_RGBA8, textureSize));
        m_offscreenTexture->setFilter(GL_LINEAR);
        m_offscreenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_offscreenTexture->setYInverted(true);
        m_offscreenTarget.reset(new GLRenderTarget(*m_offscreenTexture));
    }

    GLRenderTarget::pushRenderTarget(m_offscreenTarget.data());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(geometry);
    ScreenPaintData data(projectionMatrix);

    // The thumbnail must be rendered using kwin's opengl context as VAOs are not
    // shared across contexts. Unfortunately, this also introduces a latency of 1
    // frame, which is not ideal, but it is acceptable for things such as thumbnails.
    auto const mask = Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_SCREEN_TRANSFORMED;
    effects->paintDesktop(m_desktop, static_cast<int>(mask), infiniteRegion(), data);
    GLRenderTarget::popRenderTarget();

    // The fence is needed to avoid the case where qtquick renderer starts using
    // the texture while all rendering commands to it haven't completed yet.
    m_acquireFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // We know that the texture has changed, so schedule an item update.
    update();
}

} // namespace KWin
