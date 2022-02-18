/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "outline.h"

#include "compositor.h"
#include "platform.h"

#include "base/logging.h"
#include "base/platform.h"
#include "main.h"
#include "scripting/platform.h"
#include "win/space.h"

#include <KConfigGroup>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <cassert>

namespace KWin::render
{

outline::outline()
    : m_active(false)
{
    assert(render::compositor::self());
    connect(render::compositor::self(),
            &render::compositor::compositingToggled,
            this,
            &outline::compositingChanged);
}

outline::~outline()
{
}

void outline::show()
{
    if (m_visual.isNull()) {
        createHelper();
    }
    if (m_visual.isNull()) {
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
    if (m_visual.isNull()) {
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

void outline::createHelper()
{
    if (!m_visual.isNull()) {
        return;
    }
    m_visual.reset(kwinApp()->get_base().render->createOutline(this));
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

composited_outline_visual::composited_outline_visual(render::outline* outline)
    : outline_visual(outline)
    , m_qmlContext()
    , m_qmlComponent()
    , m_mainItem()
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
        m_qmlContext.reset(new QQmlContext(workspace()->scripting->qmlEngine()));
        m_qmlContext->setContextProperty(QStringLiteral("outline"), get_outline());
    }
    if (m_qmlComponent.isNull()) {
        m_qmlComponent.reset(new QQmlComponent(workspace()->scripting->qmlEngine()));
        const QString fileName = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation,
            kwinApp()
                ->config()
                ->group(QStringLiteral("Outline"))
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
