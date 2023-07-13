/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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

void KscreenEffect::prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime)
{
    if (m_state == StateFadingIn || m_state == StateFadingOut) {
        m_timeLine.advance(presentTime);
        if (m_timeLine.done()) {
            switchState();
        }
    }

    effects->prePaintScreen(data, presentTime);
}

void KscreenEffect::postPaintScreen()
{
    if (m_state == StateFadingIn || m_state == StateFadingOut) {
        effects->addRepaintFull();
    }
}

void KscreenEffect::prePaintWindow(effect::window_prepaint_data& data,
                                   std::chrono::milliseconds presentTime)
{
    if (m_state != StateNormal) {
        data.set_translucent();
    }
    effects->prePaintWindow(data, presentTime);
}

void KscreenEffect::paintWindow(effect::window_paint_data& data)
{
    // fade to black and fully opaque
    switch (m_state) {
    case StateFadingOut:
        data.paint.opacity = data.paint.opacity + (1.0 - data.paint.opacity) * m_timeLine.value();
        data.paint.brightness *= 1.0 - m_timeLine.value();
        break;
    case StateFadedOut:
        data.paint.opacity = 0.0;
        data.paint.brightness = 0.0;
        break;
    case StateFadingIn:
        data.paint.opacity
            = data.paint.opacity + (1.0 - data.paint.opacity) * (1.0 - m_timeLine.value());
        data.paint.brightness *= m_timeLine.value();
        break;
    default:
        // no adjustment
        break;
    }
    effects->paintWindow(data);
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
