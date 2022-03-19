/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

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
#include "kscreen.h"

// KConfigSkeleton
#include "kscreenconfig.h"

#include <kwineffects/effects_handler.h>

#include <QLoggingCategory>

/**
 * How this effect works:
 *
 * Effect announces that it is around through property _KDE_KWIN_KSCREEN_SUPPORT on the root window.
 *
 * KScreen watches for this property and when it wants to adjust screens, KScreen goes
 * through the following protocol:
 * 1. KScreen sets the property value to 1
 * 2. Effect starts to fade out all windows
 * 3. When faded out the effect sets property value to 2
 * 4. KScreen adjusts the screens
 * 5. KScreen sets property value to 3
 * 6. Effect starts to fade in all windows again
 * 7. Effect sets back property value to 0
 *
 * The property has type 32 bits cardinal. To test it use:
 * xprop -root -f _KDE_KWIN_KSCREEN_SUPPORT 32c -set _KDE_KWIN_KSCREEN_SUPPORT 1
 *
 * The states are:
 * 0: normal
 * 1: fading out
 * 2: faded out
 * 3: fading in
 */

Q_LOGGING_CATEGORY(KWIN_KSCREEN, "kwin_effect_kscreen", QtWarningMsg)

namespace KWin
{

void update_function(KscreenEffect& effect, KWin::effect::fade_update const& update)
{
    assert(!update.base.window);

    effect.m_state = KscreenEffect::StateNormal;

    if (update.value == -1) {
        effect.m_state = KscreenEffect::StateFadedOut;
    } else if (update.value == -0.5) {
        effect.m_state = KscreenEffect::StateFadingOut;
        effect.m_timeLine.reset();
    } else if (update.value == 0.5) {
        effect.m_state = KscreenEffect::StateFadingIn;
        effect.m_timeLine.reset();
    }

    effects->addRepaintFull();
}

KscreenEffect::KscreenEffect()
    : Effect()
    , m_lastPresentTime(std::chrono::milliseconds::zero())
{
    initConfig<KscreenConfig>();

    auto& kscreen_integration = effects->get_kscreen_integration();
    auto update = [this](auto&& data) { update_function(*this, data); };
    kscreen_integration.add(*this, update);

    reconfigure(ReconfigureAll);
}

KscreenEffect::~KscreenEffect()
{
}

void KscreenEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    KscreenConfig::self()->read();
    m_timeLine.setDuration(std::chrono::milliseconds(animationTime<KscreenConfig>(250)));
}

void KscreenEffect::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    std::chrono::milliseconds delta = std::chrono::milliseconds::zero();
    if (m_lastPresentTime.count()) {
        delta = presentTime - m_lastPresentTime;
    }

    if (m_state == StateFadingIn || m_state == StateFadingOut) {
        m_timeLine.update(delta);
        if (m_timeLine.done()) {
            switchState();
        }
    }

    if (isActive()) {
        m_lastPresentTime = presentTime;
    } else {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    }

    effects->prePaintScreen(data, presentTime);
}

void KscreenEffect::postPaintScreen()
{
    if (m_state == StateFadingIn || m_state == StateFadingOut) {
        effects->addRepaintFull();
    }
}

void KscreenEffect::prePaintWindow(EffectWindow* w,
                                   WindowPrePaintData& data,
                                   std::chrono::milliseconds presentTime)
{
    if (m_state != StateNormal) {
        data.setTranslucent();
    }
    effects->prePaintWindow(w, data, presentTime);
}

void KscreenEffect::paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data)
{
    // fade to black and fully opaque
    switch (m_state) {
    case StateFadingOut:
        data.setOpacity(data.opacity() + (1.0 - data.opacity()) * m_timeLine.value());
        data.multiplyBrightness(1.0 - m_timeLine.value());
        break;
    case StateFadedOut:
        data.multiplyOpacity(0.0);
        data.multiplyBrightness(0.0);
        break;
    case StateFadingIn:
        data.setOpacity(data.opacity() + (1.0 - data.opacity()) * (1.0 - m_timeLine.value()));
        data.multiplyBrightness(m_timeLine.value());
        break;
    default:
        // no adjustment
        break;
    }
    effects->paintWindow(w, mask, region, data);
}

void KscreenEffect::switchState()
{
    if (m_state == StateFadingOut) {
        m_state = StateFadedOut;
        effects->get_kscreen_integration().change_state(*this, -1);
    } else if (m_state == StateFadingIn) {
        m_state = StateNormal;
        effects->get_kscreen_integration().change_state(*this, 1);
    }
}

bool KscreenEffect::isActive() const
{
    return m_state != StateNormal;
}

} // namespace KWin
