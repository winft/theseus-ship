/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_QPA_INTEGRATION_H
#define KWIN_QPA_INTEGRATION_H

#include <epoxy/egl.h>

#include <qpa/qplatformintegration.h>
#include <QtGui/private/qgenericunixservices_p.h>
#include <QObject>

namespace KWin
{
namespace QPA
{

class Screen;

class Integration : public QObject, public QPlatformIntegration
{
    Q_OBJECT
public:
    explicit Integration();
    ~Integration() override;

    bool hasCapability(Capability cap) const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;
    QPlatformFontDatabase *fontDatabase() const override;
    QStringList themeNames() const override;
    QPlatformTheme *createPlatformTheme(const QString &name) const override;
    QPlatformOpenGLContext* createPlatformOpenGLContext(QOpenGLContext* context) const override;
    QPlatformNativeInterface *nativeInterface() const override;
    QPlatformServices *services() const override;
    void initialize() override;

    QVector<Screen*> screens() const;

private:
    void initScreens();

    std::unique_ptr<QPlatformFontDatabase> m_fontDb;
    QScopedPointer<QPlatformNativeInterface> m_nativeInterface;
    Screen *m_dummyScreen = nullptr;
    QVector<Screen*> m_screens;
    QScopedPointer<QGenericUnixServices> m_services;
};

}
}

#endif
