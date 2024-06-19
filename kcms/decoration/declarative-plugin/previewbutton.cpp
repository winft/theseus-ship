/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#include "previewbutton.h"
#include "previewbridge.h"
#include "previewclient.h"
#include "previewsettings.h"

#include <KDecoration2/Decoration>

#include <QPainter>

namespace KDecoration2
{

namespace Preview
{

PreviewButtonItem::PreviewButtonItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
}

PreviewButtonItem::~PreviewButtonItem() = default;

void PreviewButtonItem::setType(int type)
{
    setType(KDecoration2::DecorationButtonType(type));
}

void PreviewButtonItem::setType(KDecoration2::DecorationButtonType type)
{
    if (m_type == type) {
        return;
    }
    m_type = type;
    Q_EMIT typeChanged();
}

KDecoration2::DecorationButtonType PreviewButtonItem::type() const
{
    return m_type;
}

PreviewBridge* PreviewButtonItem::bridge() const
{
    return m_bridge.data();
}

void PreviewButtonItem::setBridge(PreviewBridge* bridge)
{
    if (m_bridge == bridge) {
        return;
    }
    m_bridge = bridge;
    Q_EMIT bridgeChanged();
}

Settings* PreviewButtonItem::settings() const
{
    return m_settings.data();
}

void PreviewButtonItem::setSettings(Settings* settings)
{
    if (m_settings == settings) {
        return;
    }
    m_settings = settings;
    Q_EMIT settingsChanged();
}

int PreviewButtonItem::typeAsInt() const
{
    return int(m_type);
}

void PreviewButtonItem::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    createButton();
}

void PreviewButtonItem::createButton()
{
    if (m_type == KDecoration2::DecorationButtonType::Custom || m_decoration || !m_settings
        || !m_bridge) {
        return;
    }
    m_decoration = m_bridge->createDecoration(this);
    if (!m_decoration) {
        return;
    }
    auto client = m_bridge->lastCreatedClient();
    client->setMinimizable(true);
    client->setMaximizable(true);
    client->setActive(false);
    client->setProvidesContextHelp(true);
    m_decoration->setSettings(m_settings->settings());
    m_decoration->init();
    m_button = m_bridge->createButton(m_decoration, m_type);
    connect(this, &PreviewButtonItem::widthChanged, this, &PreviewButtonItem::syncGeometry);
    connect(this, &PreviewButtonItem::heightChanged, this, &PreviewButtonItem::syncGeometry);
    syncGeometry();
}

void PreviewButtonItem::syncGeometry()
{
    if (!m_button) {
        return;
    }
    m_button->setGeometry(QRect(0, 0, width(), height()));
}

void PreviewButtonItem::paint(QPainter* painter)
{
    if (!m_button) {
        return;
    }

    const QRect rect(0, 0, width(), height());
    if (type() == KDecoration2::DecorationButtonType::Spacer) {
        static const QIcon icon = QIcon::fromTheme(QStringLiteral("distribute-horizontal"));
        icon.paint(painter, rect);
    } else {
        m_button->paint(painter, rect);
    }

    painter->setCompositionMode(QPainter::CompositionMode_SourceAtop);
    painter->fillRect(rect, m_color);
}

void PreviewButtonItem::setColor(const QColor& color)
{
    m_color = color;
    m_color.setAlpha(127);
    update();
}

}
}

#include "moc_previewbutton.cpp"
