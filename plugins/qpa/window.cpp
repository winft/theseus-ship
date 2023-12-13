/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "base/singleton_interface.h"
#include <base/platform_qobject.h>

#include <logging.h>

#include <QOpenGLFramebufferObject>
#include <qpa/qwindowsysteminterface.h>

namespace KWin
{
namespace QPA
{
static quint32 s_windowId = 0;

Window::Window(QWindow* window)
    : QPlatformWindow(window)
    , m_windowId(++s_windowId)
    , m_scale(base::singleton_interface::platform->get_scale())
{
    Q_ASSERT(!window->property("_KWIN_WINDOW_IS_OFFSCREEN").toBool());
}

Window::~Window()
{
    unmap();
}

void Window::setVisible(bool visible)
{
    if (visible) {
        map();
    } else {
        unmap();
    }

    QPlatformWindow::setVisible(visible);
}

void Window::requestActivateWindow()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
    QWindowSystemInterface::handleWindowActivated(window());
#else
    QWindowSystemInterface::handleFocusWindowChanged(window());
#endif
}

void Window::setGeometry(const QRect& rect)
{
    const QRect& oldRect = geometry();
    QPlatformWindow::setGeometry(rect);
    if (rect.x() != oldRect.x()) {
        Q_EMIT window()->xChanged(rect.x());
    }
    if (rect.y() != oldRect.y()) {
        Q_EMIT window()->yChanged(rect.y());
    }
    if (rect.width() != oldRect.width()) {
        Q_EMIT window()->widthChanged(rect.width());
    }
    if (rect.height() != oldRect.height()) {
        Q_EMIT window()->heightChanged(rect.height());
    }

    const QSize nativeSize = rect.size() * m_scale;

    if (m_contentFBO) {
        if (m_contentFBO->size() != nativeSize) {
            m_resized = true;
        }
    }
    QWindowSystemInterface::handleGeometryChange(window(), geometry());
}

WId Window::winId() const
{
    return m_windowId;
}

qreal Window::devicePixelRatio() const
{
    return m_scale;
}

void Window::bindContentFBO()
{
    if (m_resized || !m_contentFBO) {
        createFBO();
    }
    m_contentFBO->bind();
}

std::shared_ptr<QOpenGLFramebufferObject> const& Window::contentFBO() const
{
    return m_contentFBO;
}

std::shared_ptr<QOpenGLFramebufferObject> Window::swapFBO()
{
    auto fbo = m_contentFBO;
    m_contentFBO.reset();
    return fbo;
}

win::internal_window_singleton* Window::client() const
{
    return m_handle;
}

void Window::createFBO()
{
    const QRect& r = geometry();
    if (m_contentFBO && r.size().isEmpty()) {
        return;
    }
    const QSize nativeSize = r.size() * m_scale;
    m_contentFBO.reset(new QOpenGLFramebufferObject(
        nativeSize.width(), nativeSize.height(), QOpenGLFramebufferObject::CombinedDepthStencil));
    if (!m_contentFBO->isValid()) {
        qCWarning(KWIN_QPA) << "Content FBO is not valid";
    }
    m_resized = false;
}

void Window::map()
{
    if (m_handle) {
        return;
    }

    m_handle = win::singleton_interface::create_internal_window(window());
}

void Window::unmap()
{
    if (!m_handle) {
        return;
    }

    m_handle->destroy();
    m_handle = nullptr;

    m_contentFBO = nullptr;
}

}
}
