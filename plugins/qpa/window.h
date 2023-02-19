/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_QPA_WINDOW_H
#define KWIN_QPA_WINDOW_H

#include "win/singleton_interface.h"

#include <epoxy/gl.h>

#include <QPointer>
#include <memory>
#include <qpa/qplatformwindow.h>

class QOpenGLFramebufferObject;

namespace KWin::QPA
{

class Window : public QPlatformWindow
{
public:
    explicit Window(QWindow *window);
    ~Window() override;

    void setVisible(bool visible) override;
    void setGeometry(const QRect &rect) override;
    WId winId() const override;
    qreal devicePixelRatio() const override;
    void requestActivateWindow() override;

    void bindContentFBO();
    std::shared_ptr<QOpenGLFramebufferObject> const& contentFBO() const;
    std::shared_ptr<QOpenGLFramebufferObject> swapFBO();

    win::internal_window_singleton* client() const;

private:
    void createFBO();
    void map();
    void unmap();

    QPointer<win::internal_window_singleton> m_handle;
    std::shared_ptr<QOpenGLFramebufferObject> m_contentFBO;
    quint32 m_windowId;
    bool m_resized = false;
    int m_scale = 1;
};

}

#endif
