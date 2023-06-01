/*
SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "outline.h"

#include "base/logging.h"
#include "config-kwin.h"

#include <KConfigGroup>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <cassert>

namespace KWin::render
{

outline::outline(outline_visual_factory visual_factory)
    : visual_factory{visual_factory}
{
}

outline::~outline()
{
}

void outline::show()
{
    if (!m_visual) {
        m_visual = visual_factory();
    }
    if (!m_visual) {
        // something went wrong
        return;
    }
    m_visual->show();
    m_active = true;
    Q_EMIT activeChanged();
}

void outline::hide()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    Q_EMIT activeChanged();
    if (!m_visual) {
        return;
    }
    m_visual->hide();
}

void outline::show(const QRect& outlineGeometry)
{
    show(outlineGeometry, QRect());
}

void outline::show(const QRect& outlineGeometry, const QRect& visualParentGeometry)
{
    setGeometry(outlineGeometry);
    setVisualParentGeometry(visualParentGeometry);
    show();
}

void outline::setGeometry(const QRect& outlineGeometry)
{
    if (m_outlineGeometry == outlineGeometry) {
        return;
    }
    m_outlineGeometry = outlineGeometry;
    Q_EMIT geometryChanged();
    Q_EMIT unifiedGeometryChanged();
}

void outline::setVisualParentGeometry(const QRect& visualParentGeometry)
{
    if (m_visualParentGeometry == visualParentGeometry) {
        return;
    }
    m_visualParentGeometry = visualParentGeometry;
    Q_EMIT visualParentGeometryChanged();
    Q_EMIT unifiedGeometryChanged();
}

QRect outline::unifiedGeometry() const
{
    return m_outlineGeometry | m_visualParentGeometry;
}

void outline::compositingChanged()
{
    m_visual.reset();
    if (m_active) {
        show();
    }
}

outline_visual::outline_visual(render::outline* outline)
    : m_outline(outline)
{
}

outline_visual::~outline_visual()
{
}

composited_outline_visual::composited_outline_visual(render::outline* outline,
                                                     QQmlEngine& engine,
                                                     base::config& config)
    : outline_visual(outline)
    , m_qmlContext()
    , m_qmlComponent()
    , m_mainItem()
    , engine{engine}
    , config{config}
{
}

composited_outline_visual::~composited_outline_visual()
{
}

void composited_outline_visual::hide()
{
    if (QQuickWindow* w = qobject_cast<QQuickWindow*>(m_mainItem.data())) {
        w->hide();
        w->destroy();
    }
}

void composited_outline_visual::show()
{
    if (m_qmlContext.isNull()) {
        m_qmlContext.reset(new QQmlContext(&engine));
        m_qmlContext->setContextProperty(QStringLiteral("outline"), get_outline());
    }
    if (m_qmlComponent.isNull()) {
        m_qmlComponent.reset(new QQmlComponent(&engine));
        const QString fileName = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation,
            config.main->group(QStringLiteral("Outline"))
                .readEntry("QmlPath", QStringLiteral(KWIN_NAME "/outline/plasma/outline.qml")));
        if (fileName.isEmpty()) {
            qCDebug(KWIN_CORE) << "Could not locate outline.qml";
            return;
        }
        m_qmlComponent->loadUrl(QUrl::fromLocalFile(fileName));
        if (m_qmlComponent->isError()) {
            qCDebug(KWIN_CORE) << "Component failed to load: " << m_qmlComponent->errors();
        } else {
            m_mainItem.reset(m_qmlComponent->create(m_qmlContext.data()));
        }
        if (auto w = qobject_cast<QQuickWindow*>(m_mainItem.data())) {
            w->setProperty("__kwin_outline", true);
        }
    }
}

} // namespace
