/*
    SPDX-FileCopyrightText: 2022 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "blendchanges.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/utils.h>

#include <QDBusConnection>
#include <QTimer>

namespace KWin
{
BlendChanges::BlendChanges()
    : OffscreenEffect()
{
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/BlendChanges"),
                                                 QStringLiteral("org.kde.KWin.BlendChanges"),
                                                 this,
                                                 QDBusConnection::ExportAllSlots);

    setLive(false);
    m_timeline.setEasingCurve(QEasingCurve::InOutCubic);
}

BlendChanges::~BlendChanges() = default;

bool BlendChanges::supported()
{
    return effects->isOpenGLCompositing() && effects->animationsSupported();
}

void KWin::BlendChanges::start(int delay)
{
    int animationDuration = animationTime(400);

    if (!supported() || m_state != Off) {
        return;
    }
    if (effects->hasActiveFullScreenEffect()) {
        return;
    }

    auto const allWindows = effects->stackingOrder();
    for (auto window : allWindows) {
        if (!window->isFullScreen()) {
            redirect(window);
        }
    }

    QTimer::singleShot(delay, this, [this, animationDuration]() {
        m_timeline.setDuration(std::chrono::milliseconds(animationDuration));
        effects->addRepaintFull();
        m_state = Blending;
    });

    m_state = ShowingCache;
}

void BlendChanges::drawWindow(effect::window_paint_data& data)
{
    // draw the new picture underneath at full opacity
    if (m_state != ShowingCache) {
        Effect::drawWindow(data);
    }
    // then the old on top, it works better than changing both alphas with the current blend mode
    if (m_state != Off) {
        OffscreenEffect::drawWindow(data);
    }
}

void BlendChanges::apply(effect::window_paint_data& data, WindowQuadList& /*quads*/)
{
    data.paint.opacity = 1.0 - m_timeline.value() * data.paint.opacity;
}

bool BlendChanges::isActive() const
{
    return m_state != Off;
}

void BlendChanges::postPaintScreen()
{
    if (m_timeline.done()) {
        m_timeline.reset();
        m_state = Off;

        auto const allWindows = effects->stackingOrder();
        for (auto window : allWindows) {
            unredirect(window);
        }
    }
    effects->addRepaintFull();
}

void BlendChanges::prePaintScreen(effect::screen_prepaint_data& data)
{
    if (m_state == Off) {
        return;
    }
    if (m_state == Blending) {
        m_timeline.advance(data.present_time);
    }

    effects->prePaintScreen(data);
}

} // namespace KWin
