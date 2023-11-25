/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <render/effect/interface/effect_frame.h>
#include <render/effect/interface/offscreen_quick_view.h>

#include <QFont>
#include <QIcon>

namespace KWin
{

class EffectsHandler;

namespace render
{

class effect_frame_quick_scene : public OffscreenQuickScene
{
    Q_OBJECT

    Q_PROPERTY(QFont font READ font NOTIFY fontChanged)
    Q_PROPERTY(QIcon icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(QSize iconSize READ iconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(qreal frameOpacity READ frameOpacity NOTIFY frameOpacityChanged)
    Q_PROPERTY(bool crossFadeEnabled READ crossFadeEnabled NOTIFY crossFadeEnabledChanged)
    Q_PROPERTY(qreal crossFadeProgress READ crossFadeProgress NOTIFY crossFadeProgressChanged)

public:
    effect_frame_quick_scene(EffectFrameStyle style,
                             bool staticSize,
                             QPoint position,
                             Qt::Alignment alignment);
    ~effect_frame_quick_scene() override;

    EffectFrameStyle style() const;
    bool isStatic() const;

    // has to be const-ref to match effect_frame_impl...
    const QFont& font() const;
    void setFont(const QFont& font);
    Q_SIGNAL void fontChanged(const QFont& font);

    const QIcon& icon() const;
    void setIcon(const QIcon& icon);
    Q_SIGNAL void iconChanged(const QIcon& icon);

    const QSize& iconSize() const;
    void setIconSize(const QSize& iconSize);
    Q_SIGNAL void iconSizeChanged(const QSize& iconSize);

    const QString& text() const;
    void setText(const QString& text);
    Q_SIGNAL void textChanged(const QString& text);

    qreal frameOpacity() const;
    void setFrameOpacity(qreal frameOpacity);
    Q_SIGNAL void frameOpacityChanged(qreal frameOpacity);

    bool crossFadeEnabled() const;
    void setCrossFadeEnabled(bool enabled);
    Q_SIGNAL void crossFadeEnabledChanged(bool enabled);

    qreal crossFadeProgress() const;
    void setCrossFadeProgress(qreal progress);
    Q_SIGNAL void crossFadeProgressChanged(qreal progress);

    Qt::Alignment alignment() const;
    void setAlignment(Qt::Alignment alignment);

    QPoint position() const;
    void setPosition(const QPoint& point);

private:
    void reposition();

    EffectFrameStyle m_style;

    // Position
    bool m_static;
    QPoint m_point;
    Qt::Alignment m_alignment;

    // Contents
    QFont m_font;
    QIcon m_icon;
    QSize m_iconSize;
    QString m_text;
    qreal m_frameOpacity = 0.0;
    bool m_crossFadeEnabled = false;
    qreal m_crossFadeProgress = 0.0;
};

class KWIN_EXPORT effect_frame_impl : public QObject, public EffectFrame
{
    Q_OBJECT
public:
    explicit effect_frame_impl(EffectsHandler& effects,
                               EffectFrameStyle style,
                               bool staticSize = true,
                               QPoint position = QPoint(-1, -1),
                               Qt::Alignment alignment = Qt::AlignCenter);
    ~effect_frame_impl() override;

    void free() override;
    void render(const QRegion& region = infiniteRegion(),
                double opacity = 1.0,
                double frameOpacity = 1.0) override;
    Qt::Alignment alignment() const override;
    void setAlignment(Qt::Alignment alignment) override;
    const QFont& font() const override;
    void setFont(const QFont& font) override;
    const QRect& geometry() const override;
    void setGeometry(const QRect& geometry, bool force = false) override;
    const QIcon& icon() const override;
    void setIcon(const QIcon& icon) override;
    const QSize& iconSize() const override;
    void setIconSize(const QSize& size) override;
    void setPosition(const QPoint& point) override;
    const QString& text() const override;
    void setText(const QString& text) override;
    EffectFrameStyle style() const override;
    bool isCrossFade() const override;
    void enableCrossFade(bool enable) override;
    qreal crossFadeProgress() const override;
    void setCrossFadeProgress(qreal progress) override;

    EffectsHandler& effects;

private:
    // As we need to use Qt slots we cannot copy this class.
    Q_DISABLE_COPY(effect_frame_impl)

    effect_frame_quick_scene* m_view;
    QRect m_geometry;
};

}
}
