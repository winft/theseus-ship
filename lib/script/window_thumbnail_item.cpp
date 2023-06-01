/*
SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Sergio Blanco <seral79@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_thumbnail_item.h"

#include "scripting_logging.h"
#include "singleton_interface.h"
#include "space.h"

#include "render/compositor_qobject.h"
#include "render/singleton_interface.h"

#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>
#include <kwingl/texture.h>
#include <kwingl/utils.h>

#include <QQuickWindow>
#include <QRunnable>
#include <QSGImageNode>
#include <QSGTextureProvider>

namespace KWin::scripting
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
        m_texture.reset(QNativeInterface::QSGOpenGLTexture::fromNative(
            textureId, m_window, nativeTexture->size(), QQuickWindow::TextureHasAlphaChannel));
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

window_thumbnail_item::window_thumbnail_item(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    update_render_notifier();

    connect(render::singleton_interface::compositor,
            &render::compositor_qobject::aboutToToggleCompositing,
            this,
            &window_thumbnail_item::destroyOffscreenTexture);
    connect(render::singleton_interface::compositor,
            &render::compositor_qobject::compositingToggled,
            this,
            &window_thumbnail_item::update_render_notifier);
    connect(this, &QQuickItem::windowChanged, this, &window_thumbnail_item::update_render_notifier);
}

window_thumbnail_item::~window_thumbnail_item()
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

void window_thumbnail_item::releaseResources()
{
    if (m_provider) {
        window()->scheduleRenderJob(new ThumbnailTextureProviderCleanupJob(m_provider),
                                    QQuickWindow::AfterSynchronizingStage);
        m_provider = nullptr;
    }
}

bool window_thumbnail_item::isTextureProvider() const
{
    return true;
}

QSGTextureProvider* window_thumbnail_item::textureProvider() const
{
    if (QQuickItem::isTextureProvider()) {
        return QQuickItem::textureProvider();
    }
    if (!m_provider) {
        m_provider = new ThumbnailTextureProvider(window());
    }
    return m_provider;
}

void window_thumbnail_item::update_render_notifier()
{
    disconnect(render_notifier);

    if (!window()) {
        return;
    }

    if (!use_gl_thumbnails()) {
        return;
    }

    render_notifier = connect(effects,
                              &EffectsHandler::frameRendered,
                              this,
                              &window_thumbnail_item::updateOffscreenTexture);
}

bool window_thumbnail_item::use_gl_thumbnails() const
{
    static bool qt_quick_is_software
        = QStringList({QStringLiteral("software"), QStringLiteral("softwarecontext")})
              .contains(QQuickWindow::sceneGraphBackend());
    return effects && effects->isOpenGLCompositing() && !qt_quick_is_software;
}

QSize window_thumbnail_item::sourceSize() const
{
    return m_sourceSize;
}

void window_thumbnail_item::setSourceSize(const QSize& sourceSize)
{
    if (m_sourceSize != sourceSize) {
        m_sourceSize = sourceSize;
        invalidateOffscreenTexture();
        Q_EMIT sourceSizeChanged();
    }
}

void window_thumbnail_item::destroyOffscreenTexture()
{
    if (!use_gl_thumbnails()) {
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

QSGNode* window_thumbnail_item::updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData*)
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

QUuid window_thumbnail_item::wId() const
{
    return m_wId;
}

scripting::window* find_controlled_window(QUuid const& wId)
{
    auto const windows = singleton_interface::qt_script_space->clientList();
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
    } else {
        if (m_client) {
            m_client = nullptr;
            updateImplicitSize();
            Q_EMIT clientChanged();
        }
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
        m_offscreenTarget.reset(new GLRenderTarget(m_offscreenTexture.data()));
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

} // namespace KWin
