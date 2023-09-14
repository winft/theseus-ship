/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "backingstore.h"
#include "window.h"

#include "win/internal_window.h"

namespace KWin
{
namespace QPA
{

BackingStore::BackingStore(QWindow* window)
    : QPlatformBackingStore(window)
{
}

BackingStore::~BackingStore() = default;

QPaintDevice* BackingStore::paintDevice()
{
    return &m_buffer;
}

void BackingStore::resize(const QSize& size, const QRegion& staticContents)
{
    Q_UNUSED(staticContents)

    if (m_buffer.size() == size) {
        return;
    }

    const QPlatformWindow* platformWindow = static_cast<QPlatformWindow*>(window()->handle());
    const qreal devicePixelRatio = platformWindow->devicePixelRatio();

    m_buffer = QImage(size * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
    m_buffer.setDevicePixelRatio(devicePixelRatio);
}

static void blitImage(const QImage& source, QImage& target, const QRegion& region)
{
    for (const QRect& rect : region) {
        blitImage(source, target, rect);
    }
}

void BackingStore::flush(QWindow* window, const QRegion& region, const QPoint& offset)
{
    Q_UNUSED(offset)

    Window* platformWindow = static_cast<Window*>(window->handle());
    auto* client = platformWindow->client();
    if (!client) {
        return;
    }

    client->present_image(m_buffer, region);
}

}
}
