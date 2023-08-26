/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "frame.h"

#include <base/config-kwin.h>
#include <render/effect/interface/effects_handler.h>

#include <QQuickItem>
#include <QStandardPaths>
#include <QUrl>

namespace KWin::render
{

effect_frame_quick_scene::effect_frame_quick_scene(EffectFrameStyle style,
                                                   bool staticSize,
                                                   QPoint position,
                                                   Qt::Alignment alignment,
                                                   QObject* parent)
    : EffectQuickScene(parent)
    , m_style(style)
    , m_static(staticSize)
    , m_point(position)
    , m_alignment(alignment)
{

    QString name;
    switch (style) {
    case EffectFrameNone:
        name = QStringLiteral("none");
        break;
    case EffectFrameUnstyled:
        name = QStringLiteral("unstyled");
        break;
    case EffectFrameStyled:
        name = QStringLiteral("styled");
        break;
    }

    const QString defaultPath = QStringLiteral(KWIN_NAME "/frames/plasma/frame_%1.qml").arg(name);
    // TODO read from kwinApp()->config() "QmlPath" like Outline/OnScreenNotification
    // *if* someone really needs this to be configurable.
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, defaultPath);

    setSource(QUrl::fromLocalFile(path),
              QVariantMap{{QStringLiteral("effectFrame"), QVariant::fromValue(this)}});

    if (rootItem()) {
        connect(rootItem(),
                &QQuickItem::implicitWidthChanged,
                this,
                &effect_frame_quick_scene::reposition);
        connect(rootItem(),
                &QQuickItem::implicitHeightChanged,
                this,
                &effect_frame_quick_scene::reposition);
    }
}

effect_frame_quick_scene::~effect_frame_quick_scene() = default;

EffectFrameStyle effect_frame_quick_scene::style() const
{
    return m_style;
}

bool effect_frame_quick_scene::isStatic() const
{
    return m_static;
}

const QFont& effect_frame_quick_scene::font() const
{
    return m_font;
}

void effect_frame_quick_scene::setFont(const QFont& font)
{
    if (m_font == font) {
        return;
    }

    m_font = font;
    Q_EMIT fontChanged(font);
    reposition();
}

const QIcon& effect_frame_quick_scene::icon() const
{
    return m_icon;
}

void effect_frame_quick_scene::setIcon(const QIcon& icon)
{
    m_icon = icon;
    Q_EMIT iconChanged(icon);
    reposition();
}

const QSize& effect_frame_quick_scene::iconSize() const
{
    return m_iconSize;
}

void effect_frame_quick_scene::setIconSize(const QSize& iconSize)
{
    if (m_iconSize == iconSize) {
        return;
    }

    m_iconSize = iconSize;
    Q_EMIT iconSizeChanged(iconSize);
    reposition();
}

const QString& effect_frame_quick_scene::text() const
{
    return m_text;
}

void effect_frame_quick_scene::setText(const QString& text)
{
    if (m_text == text) {
        return;
    }

    m_text = text;
    Q_EMIT textChanged(text);
    reposition();
}

qreal effect_frame_quick_scene::frameOpacity() const
{
    return m_frameOpacity;
}

void effect_frame_quick_scene::setFrameOpacity(qreal frameOpacity)
{
    if (m_frameOpacity != frameOpacity) {
        m_frameOpacity = frameOpacity;
        Q_EMIT frameOpacityChanged(frameOpacity);
    }
}

bool effect_frame_quick_scene::crossFadeEnabled() const
{
    return m_crossFadeEnabled;
}

void effect_frame_quick_scene::setCrossFadeEnabled(bool enabled)
{
    if (m_crossFadeEnabled != enabled) {
        m_crossFadeEnabled = enabled;
        Q_EMIT crossFadeEnabledChanged(enabled);
    }
}

qreal effect_frame_quick_scene::crossFadeProgress() const
{
    return m_crossFadeProgress;
}

void effect_frame_quick_scene::setCrossFadeProgress(qreal progress)
{
    if (m_crossFadeProgress != progress) {
        m_crossFadeProgress = progress;
        Q_EMIT crossFadeProgressChanged(progress);
    }
}

Qt::Alignment effect_frame_quick_scene::alignment() const
{
    return m_alignment;
}

void effect_frame_quick_scene::setAlignment(Qt::Alignment alignment)
{
    if (m_alignment == alignment) {
        return;
    }

    m_alignment = alignment;
    reposition();
}

QPoint effect_frame_quick_scene::position() const
{
    return m_point;
}

void effect_frame_quick_scene::setPosition(const QPoint& point)
{
    if (m_point == point) {
        return;
    }

    m_point = point;
    reposition();
}

void effect_frame_quick_scene::reposition()
{
    if (!rootItem() || m_point.x() < 0 || m_point.y() < 0) {
        return;
    }

    QSizeF size;
    if (m_static) {
        size = rootItem()->size();
    } else {
        size = QSizeF(rootItem()->implicitWidth(), rootItem()->implicitHeight());
    }

    QRect geometry(QPoint(), size.toSize());

    if (m_alignment & Qt::AlignLeft)
        geometry.moveLeft(m_point.x());
    else if (m_alignment & Qt::AlignRight)
        geometry.moveLeft(m_point.x() - geometry.width());
    else
        geometry.moveLeft(m_point.x() - geometry.width() / 2);
    if (m_alignment & Qt::AlignTop)
        geometry.moveTop(m_point.y());
    else if (m_alignment & Qt::AlignBottom)
        geometry.moveTop(m_point.y() - geometry.height());
    else
        geometry.moveTop(m_point.y() - geometry.height() / 2);

    if (geometry == this->geometry()) {
        return;
    }

    setGeometry(geometry);
}

effect_frame_impl::effect_frame_impl(EffectsHandler& effects,
                                     EffectFrameStyle style,
                                     bool staticSize,
                                     QPoint position,
                                     Qt::Alignment alignment)
    : QObject(nullptr)
    , EffectFrame()
    , effects{effects}
    , m_view{new effect_frame_quick_scene(style, staticSize, position, alignment, nullptr)}
{
    connect(m_view, &EffectQuickView::repaintNeeded, this, [this] {
        this->effects.addRepaint(geometry());
    });
    connect(m_view,
            &EffectQuickView::geometryChanged,
            this,
            [this](const QRect& oldGeometry, const QRect& newGeometry) {
                this->effects.addRepaint(oldGeometry);
                m_geometry = newGeometry;
                this->effects.addRepaint(newGeometry);
            });
}

effect_frame_impl::~effect_frame_impl()
{
    // Effects often destroy their cached TextFrames in pre/postPaintScreen.
    // Destroying an OffscreenQuickView changes GL context, which we
    // must not do during effect rendering.
    // Delay destruction of the view until after the rendering.
    m_view->deleteLater();
}

Qt::Alignment effect_frame_impl::alignment() const
{
    return m_view->alignment();
}

void effect_frame_impl::setAlignment(Qt::Alignment alignment)
{
    m_view->setAlignment(alignment);
}

const QFont& effect_frame_impl::font() const
{
    return m_view->font();
}

void effect_frame_impl::setFont(const QFont& font)
{
    m_view->setFont(font);
}

void effect_frame_impl::free()
{
    m_view->hide();
}

const QRect& effect_frame_impl::geometry() const
{
    // Can't forward to EffectQuickView::geometry() because we return a reference.
    return m_geometry;
}

void effect_frame_impl::setGeometry(const QRect& geometry, bool force)
{
    Q_UNUSED(force)
    m_view->setGeometry(geometry);
}

const QIcon& effect_frame_impl::icon() const
{
    return m_view->icon();
}

void effect_frame_impl::setIcon(const QIcon& icon)
{
    m_view->setIcon(icon);

    if (m_view->iconSize().isEmpty()
        && !icon.availableSizes().isEmpty()) { // Set a size if we don't already have one
        setIconSize(icon.availableSizes().constFirst());
    }
}

const QSize& effect_frame_impl::iconSize() const
{
    return m_view->iconSize();
}

void effect_frame_impl::setIconSize(const QSize& size)
{
    m_view->setIconSize(size);
}

void effect_frame_impl::setPosition(const QPoint& point)
{
    m_view->setPosition(point);
}

void effect_frame_impl::render(const QRegion& region, double opacity, double frameOpacity)
{
    Q_UNUSED(region);

    if (!m_view->rootItem()) {
        return;
    }

    m_view->show();

    m_view->setOpacity(opacity);
    m_view->setFrameOpacity(frameOpacity);

    effects.renderEffectQuickView(m_view);
}

const QString& effect_frame_impl::text() const
{
    return m_view->text();
}

void effect_frame_impl::setText(const QString& text)
{
    m_view->setText(text);
}

EffectFrameStyle effect_frame_impl::style() const
{
    return m_view->style();
}

bool effect_frame_impl::isCrossFade() const
{
    return m_view->crossFadeEnabled();
}

void effect_frame_impl::enableCrossFade(bool enable)
{
    m_view->setCrossFadeEnabled(enable);
}

qreal effect_frame_impl::crossFadeProgress() const
{
    return m_view->crossFadeProgress();
}

void effect_frame_impl::setCrossFadeProgress(qreal progress)
{
    m_view->setCrossFadeProgress(progress);
}

}
