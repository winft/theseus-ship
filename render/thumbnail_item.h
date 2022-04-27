/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>

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

#include "kwin_export.h"

#include <QQuickItem>
#include <QUuid>

#include <epoxy/gl.h>

namespace KWin
{

class EffectWindow;
class GLRenderTarget;
class GLTexture;

namespace scripting
{
class window;
}

namespace render
{
class ThumbnailTextureProvider;

class KWIN_EXPORT basic_thumbnail_item : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QSize sourceSize READ sourceSize WRITE setSourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(qreal brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QQuickItem* clipTo READ clipTo WRITE setClipTo NOTIFY clipToChanged)
public:
    explicit basic_thumbnail_item(QQuickItem* parent = nullptr);
    ~basic_thumbnail_item() override;

    qreal brightness() const
    {
        return 1;
    }
    void setBrightness(qreal brightness);

    qreal saturation() const
    {
        return 1;
    }
    void setSaturation(qreal saturation);

    QQuickItem* clipTo() const
    {
        return nullptr;
    }
    void setClipTo(QQuickItem* clip);

    QSize sourceSize() const;
    void setSourceSize(const QSize& sourceSize);

    QSGTextureProvider* textureProvider() const override;
    bool isTextureProvider() const override;
    QSGNode* updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData*) override;

Q_SIGNALS:
    void brightnessChanged();
    void saturationChanged();
    void clipToChanged();
    void sourceSizeChanged();

protected:
    void releaseResources() override;

    virtual QImage fallbackImage() const = 0;
    virtual QRectF paintedRect() const = 0;
    virtual void invalidateOffscreenTexture() = 0;
    virtual void updateOffscreenTexture() = 0;
    void destroyOffscreenTexture();

    mutable ThumbnailTextureProvider* m_provider = nullptr;
    QSharedPointer<GLTexture> m_offscreenTexture;
    QScopedPointer<GLRenderTarget> m_offscreenTarget;
    GLsync m_acquireFence = 0;
    qreal m_devicePixelRatio = 1;

private:
    void update_render_notifier();
    QMetaObject::Connection render_notifier;

    QSize m_sourceSize;
};

class KWIN_EXPORT window_thumbnail_item : public basic_thumbnail_item
{
    Q_OBJECT
    Q_PROPERTY(QUuid wId READ wId WRITE setWId NOTIFY wIdChanged)
    Q_PROPERTY(KWin::scripting::window* client READ client WRITE setClient NOTIFY clientChanged)
public:
    explicit window_thumbnail_item(QQuickItem* parent = nullptr);

    QUuid wId() const;
    void setWId(const QUuid& wId);

    scripting::window* client() const;
    void setClient(scripting::window* window);

Q_SIGNALS:
    void wIdChanged();
    void clientChanged();

protected:
    QImage fallbackImage() const override;
    QRectF paintedRect() const override;
    void invalidateOffscreenTexture() override;
    void updateOffscreenTexture() override;
    void updateImplicitSize();

private:
    QUuid m_wId;
    QPointer<scripting::window> m_client;
    bool m_dirty = false;
};

}
}
