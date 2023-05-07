/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "base/options.h"

namespace KWin::scripting
{

options::options(base::options& base)
    : base{base}
{
    auto qbase = base.qobject.get();
    QObject::connect(
        qbase, &base::options_qobject::focusPolicyChanged, this, &options::focusPolicyChanged);
    QObject::connect(qbase,
                     &base::options_qobject::focusPolicyIsResonableChanged,
                     this,
                     &options::focusPolicyIsResonableChanged);
    QObject::connect(qbase,
                     &base::options_qobject::nextFocusPrefersMouseChanged,
                     this,
                     &options::nextFocusPrefersMouseChanged);

    QObject::connect(
        qbase, &base::options_qobject::clickRaiseChanged, this, &options::clickRaiseChanged);
    QObject::connect(
        qbase, &base::options_qobject::autoRaiseChanged, this, &options::autoRaiseChanged);
    QObject::connect(qbase,
                     &base::options_qobject::autoRaiseIntervalChanged,
                     this,
                     &options::autoRaiseIntervalChanged);
    QObject::connect(qbase,
                     &base::options_qobject::delayFocusIntervalChanged,
                     this,
                     &options::delayFocusIntervalChanged);
    QObject::connect(qbase,
                     &base::options_qobject::separateScreenFocusChanged,
                     this,
                     &options::separateScreenFocusChanged);

    QObject::connect(
        qbase, &base::options_qobject::placementChanged, this, &options::placementChanged);
    QObject::connect(qbase,
                     &base::options_qobject::borderSnapZoneChanged,
                     this,
                     &options::borderSnapZoneChanged);
    QObject::connect(qbase,
                     &base::options_qobject::windowSnapZoneChanged,
                     this,
                     &options::windowSnapZoneChanged);
    QObject::connect(qbase,
                     &base::options_qobject::centerSnapZoneChanged,
                     this,
                     &options::centerSnapZoneChanged);
    QObject::connect(qbase,
                     &base::options_qobject::snapOnlyWhenOverlappingChanged,
                     this,
                     &options::snapOnlyWhenOverlappingChanged);

    QObject::connect(qbase,
                     &base::options_qobject::rollOverDesktopsChanged,
                     this,
                     &options::rollOverDesktopsChanged);
    QObject::connect(qbase,
                     &base::options_qobject::focusStealingPreventionLevelChanged,
                     this,
                     &options::focusStealingPreventionLevelChanged);

    QObject::connect(qbase,
                     &base::options_qobject::operationTitlebarDblClickChanged,
                     this,
                     &options::operationTitlebarDblClickChanged);
    QObject::connect(qbase,
                     &base::options_qobject::operationMaxButtonLeftClickChanged,
                     this,
                     &options::operationMaxButtonLeftClickChanged);
    QObject::connect(qbase,
                     &base::options_qobject::operationMaxButtonRightClickChanged,
                     this,
                     &options::operationMaxButtonRightClickChanged);
    QObject::connect(qbase,
                     &base::options_qobject::operationMaxButtonMiddleClickChanged,
                     this,
                     &options::operationMaxButtonMiddleClickChanged);

    QObject::connect(qbase,
                     &base::options_qobject::commandActiveTitlebar1Changed,
                     this,
                     &options::commandActiveTitlebar1Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandActiveTitlebar2Changed,
                     this,
                     &options::commandActiveTitlebar2Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandActiveTitlebar3Changed,
                     this,
                     &options::commandActiveTitlebar3Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandInactiveTitlebar1Changed,
                     this,
                     &options::commandInactiveTitlebar1Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandInactiveTitlebar2Changed,
                     this,
                     &options::commandInactiveTitlebar2Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandInactiveTitlebar3Changed,
                     this,
                     &options::commandInactiveTitlebar3Changed);

    QObject::connect(qbase,
                     &base::options_qobject::commandWindow1Changed,
                     this,
                     &options::commandWindow1Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandWindow2Changed,
                     this,
                     &options::commandWindow2Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandWindow3Changed,
                     this,
                     &options::commandWindow3Changed);
    QObject::connect(qbase,
                     &base::options_qobject::commandWindowWheelChanged,
                     this,
                     &options::commandWindowWheelChanged);

    QObject::connect(
        qbase, &base::options_qobject::commandAll1Changed, this, &options::commandAll1Changed);
    QObject::connect(
        qbase, &base::options_qobject::commandAll2Changed, this, &options::commandAll2Changed);
    QObject::connect(
        qbase, &base::options_qobject::commandAll3Changed, this, &options::commandAll3Changed);
    QObject::connect(qbase,
                     &base::options_qobject::keyCmdAllModKeyChanged,
                     this,
                     &options::keyCmdAllModKeyChanged);

    QObject::connect(qbase,
                     &base::options_qobject::condensedTitleChanged,
                     this,
                     &options::condensedTitleChanged);

    QObject::connect(qbase,
                     &base::options_qobject::electricBorderMaximizeChanged,
                     this,
                     &options::electricBorderMaximizeChanged);
    QObject::connect(qbase,
                     &base::options_qobject::electricBorderTilingChanged,
                     this,
                     &options::electricBorderTilingChanged);
    QObject::connect(qbase,
                     &base::options_qobject::electricBorderCornerRatioChanged,
                     this,
                     &options::electricBorderCornerRatioChanged);

    QObject::connect(qbase,
                     &base::options_qobject::borderlessMaximizedWindowsChanged,
                     this,
                     &options::borderlessMaximizedWindowsChanged);
    QObject::connect(qbase,
                     &base::options_qobject::killPingTimeoutChanged,
                     this,
                     &options::killPingTimeoutChanged);
    QObject::connect(qbase,
                     &base::options_qobject::hideUtilityWindowsForInactiveChanged,
                     this,
                     &options::hideUtilityWindowsForInactiveChanged);
    QObject::connect(qbase,
                     &base::options_qobject::compositingModeChanged,
                     this,
                     &options::compositingModeChanged);
    QObject::connect(qbase,
                     &base::options_qobject::useCompositingChanged,
                     this,
                     &options::useCompositingChanged);
    QObject::connect(qbase,
                     &base::options_qobject::hiddenPreviewsChanged,
                     this,
                     &options::hiddenPreviewsChanged);

    QObject::connect(qbase,
                     &base::options_qobject::maxFpsIntervalChanged,
                     this,
                     &options::maxFpsIntervalChanged);
    QObject::connect(
        qbase, &base::options_qobject::refreshRateChanged, this, &options::refreshRateChanged);
    QObject::connect(
        qbase, &base::options_qobject::vBlankTimeChanged, this, &options::vBlankTimeChanged);
    QObject::connect(qbase,
                     &base::options_qobject::glStrictBindingChanged,
                     this,
                     &options::glStrictBindingChanged);
    QObject::connect(qbase,
                     &base::options_qobject::glStrictBindingFollowsDriverChanged,
                     this,
                     &options::glStrictBindingFollowsDriverChanged);
    QObject::connect(qbase,
                     &base::options_qobject::windowsBlockCompositingChanged,
                     this,
                     &options::windowsBlockCompositingChanged);

    QObject::connect(qbase,
                     &base::options_qobject::animationSpeedChanged,
                     this,
                     &options::animationSpeedChanged);
    QObject::connect(qbase,
                     &base::options_qobject::animationCurveChanged,
                     this,
                     &options::animationCurveChanged);

    QObject::connect(qbase, &base::options_qobject::configChanged, this, &options::configChanged);
}

options::FocusPolicy options::focusPolicy() const
{
    return static_cast<options::FocusPolicy>(base.qobject->focusPolicy());
}

bool options::isNextFocusPrefersMouse() const
{
    return base.qobject->isNextFocusPrefersMouse();
}

bool options::isClickRaise() const
{
    return base.qobject->isClickRaise();
}

bool options::isAutoRaise() const
{
    return base.qobject->isAutoRaise();
}

int options::autoRaiseInterval() const
{
    return base.qobject->autoRaiseInterval();
}

int options::delayFocusInterval() const
{
    return base.qobject->delayFocusInterval();
}

bool options::isSeparateScreenFocus() const
{
    return base.qobject->isSeparateScreenFocus();
}

win::placement options::placement() const
{
    return base.qobject->placement();
}

bool options::focusPolicyIsReasonable()
{
    return base.qobject->focusPolicyIsReasonable();
}

int options::borderSnapZone() const
{
    return base.qobject->borderSnapZone();
}

int options::windowSnapZone() const
{
    return base.qobject->windowSnapZone();
}

int options::centerSnapZone() const
{
    return base.qobject->centerSnapZone();
}

bool options::isSnapOnlyWhenOverlapping() const
{
    return base.qobject->isSnapOnlyWhenOverlapping();
}

bool options::isRollOverDesktops() const
{
    return base.qobject->isRollOverDesktops();
}

win::fsp_level options::focusStealingPreventionLevel() const
{
    return base.qobject->focusStealingPreventionLevel();
}

options::WindowOperation options::operationTitlebarDblClick() const
{
    return static_cast<options::WindowOperation>(base.qobject->operationTitlebarDblClick());
}

options::WindowOperation options::operationMaxButtonLeftClick() const
{
    return static_cast<options::WindowOperation>(base.qobject->operationMaxButtonLeftClick());
}

options::WindowOperation options::operationMaxButtonRightClick() const
{
    return static_cast<options::WindowOperation>(base.qobject->operationMaxButtonRightClick());
}

options::WindowOperation options::operationMaxButtonMiddleClick() const
{
    return static_cast<options::WindowOperation>(base.qobject->operationMaxButtonMiddleClick());
}

options::WindowOperation options::operationMaxButtonClick(Qt::MouseButtons button) const
{
    return static_cast<options::WindowOperation>(base.qobject->operationMaxButtonClick(button));
}

options::MouseCommand options::commandActiveTitlebar1() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandActiveTitlebar1());
}

options::MouseCommand options::commandActiveTitlebar2() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandActiveTitlebar2());
}

options::MouseCommand options::commandActiveTitlebar3() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandActiveTitlebar3());
}

options::MouseCommand options::commandInactiveTitlebar1() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandInactiveTitlebar1());
}

options::MouseCommand options::commandInactiveTitlebar2() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandInactiveTitlebar2());
}

options::MouseCommand options::commandInactiveTitlebar3() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandInactiveTitlebar3());
}

options::MouseCommand options::commandWindow1() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandWindow1());
}

options::MouseCommand options::commandWindow2() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandWindow2());
}

options::MouseCommand options::commandWindow3() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandWindow3());
}

options::MouseCommand options::commandWindowWheel() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandWindowWheel());
}

options::MouseCommand options::commandAll1() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandAll1());
}

options::MouseCommand options::commandAll2() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandAll2());
}
options::MouseCommand options::commandAll3() const
{
    return static_cast<options::MouseCommand>(base.qobject->commandAll3());
}

options::MouseWheelCommand options::commandAllWheel() const
{
    return static_cast<options::MouseWheelCommand>(base.qobject->commandAllWheel());
}

uint options::keyCmdAllModKey() const
{
    return base.qobject->keyCmdAllModKey();
}

Qt::KeyboardModifier options::commandAllModifier() const
{
    return base.qobject->commandAllModifier();
}

bool options::condensedTitle() const
{
    return base.qobject->condensedTitle();
}

bool options::electricBorderMaximize() const
{
    return base.qobject->electricBorderMaximize();
}

bool options::electricBorderTiling() const
{
    return base.qobject->electricBorderTiling();
}

float options::electricBorderCornerRatio() const
{
    return base.qobject->electricBorderCornerRatio();
}

bool options::borderlessMaximizedWindows() const
{
    return base.qobject->borderlessMaximizedWindows();
}

int options::killPingTimeout() const
{
    return base.qobject->killPingTimeout();
}

bool options::isHideUtilityWindowsForInactive() const
{
    return base.qobject->isHideUtilityWindowsForInactive();
}

int options::compositingMode() const
{
    return base.qobject->compositingMode();
}

void options::setCompositingMode(int mode)
{
    base.qobject->setCompositingMode(mode);
}

bool options::isUseCompositing() const
{
    return base.qobject->isUseCompositing();
}

int options::hiddenPreviews() const
{
    return static_cast<int>(base.qobject->hiddenPreviews());
}

qint64 options::maxFpsInterval() const
{
    return base.qobject->maxFpsInterval();
}

uint options::refreshRate() const
{
    return base.qobject->refreshRate();
}

qint64 options::vBlankTime() const
{
    return base.qobject->vBlankTime();
}

bool options::isGlStrictBinding() const
{
    return base.qobject->isGlStrictBinding();
}

bool options::isGlStrictBindingFollowsDriver() const
{
    return base.qobject->isGlStrictBindingFollowsDriver();
}

bool options::windowsBlockCompositing() const
{
    return base.qobject->windowsBlockCompositing();
}

options::AnimationCurve options::animationCurve() const
{
    return static_cast<options::AnimationCurve>(base.qobject->animationCurve());
}

void options::setFocusPolicy(options::FocusPolicy focusPolicy)
{
    base.qobject->setFocusPolicy(static_cast<win::focus_policy>(focusPolicy));
}

void options::setNextFocusPrefersMouse(bool nextFocusPrefersMouse)
{
    base.qobject->setNextFocusPrefersMouse(nextFocusPrefersMouse);
}

void options::setClickRaise(bool clickRaise)
{
    base.qobject->setClickRaise(clickRaise);
}

void options::setAutoRaise(bool autoRaise)
{
    base.qobject->setAutoRaise(autoRaise);
}

void options::setAutoRaiseInterval(int autoRaiseInterval)
{
    base.qobject->setAutoRaiseInterval(autoRaiseInterval);
}

void options::setDelayFocusInterval(int delayFocusInterval)
{
    base.qobject->setDelayFocusInterval(delayFocusInterval);
}

void options::setSeparateScreenFocus(bool separateScreenFocus)
{
    base.qobject->setSeparateScreenFocus(separateScreenFocus);
}

void options::setPlacement(win::placement placement)
{
    base.qobject->setPlacement(placement);
}

void options::setBorderSnapZone(int borderSnapZone)
{
    base.qobject->setBorderSnapZone(borderSnapZone);
}

void options::setWindowSnapZone(int windowSnapZone)
{
    base.qobject->setWindowSnapZone(windowSnapZone);
}

void options::setCenterSnapZone(int centerSnapZone)
{
    base.qobject->setCenterSnapZone(centerSnapZone);
}

void options::setSnapOnlyWhenOverlapping(bool snapOnlyWhenOverlapping)
{
    base.qobject->setSnapOnlyWhenOverlapping(snapOnlyWhenOverlapping);
}

void options::setRollOverDesktops(bool rollOverDesktops)
{
    base.qobject->setRollOverDesktops(rollOverDesktops);
}

void options::setFocusStealingPreventionLevel(win::fsp_level lvl)
{
    base.qobject->setFocusStealingPreventionLevel(lvl);
}

void options::setOperationTitlebarDblClick(WindowOperation op)
{
    base.qobject->setOperationTitlebarDblClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonLeftClick(WindowOperation op)
{
    base.qobject->setOperationMaxButtonLeftClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonRightClick(WindowOperation op)
{
    base.qobject->setOperationMaxButtonRightClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonMiddleClick(WindowOperation op)
{
    base.qobject->setOperationMaxButtonMiddleClick(static_cast<win::win_op>(op));
}

void options::setCommandActiveTitlebar1(MouseCommand cmd)
{
    base.qobject->setCommandActiveTitlebar1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandActiveTitlebar2(MouseCommand cmd)
{
    base.qobject->setCommandActiveTitlebar2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandActiveTitlebar3(MouseCommand cmd)
{
    base.qobject->setCommandActiveTitlebar3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar1(MouseCommand cmd)
{
    base.qobject->setCommandInactiveTitlebar1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar2(MouseCommand cmd)
{
    base.qobject->setCommandInactiveTitlebar2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar3(MouseCommand cmd)
{
    base.qobject->setCommandInactiveTitlebar3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow1(MouseCommand cmd)
{
    base.qobject->setCommandWindow1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow2(MouseCommand cmd)
{
    base.qobject->setCommandWindow2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow3(MouseCommand cmd)
{
    base.qobject->setCommandWindow3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindowWheel(MouseCommand cmd)
{
    base.qobject->setCommandWindowWheel(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll1(MouseCommand cmd)
{
    base.qobject->setCommandAll1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll2(MouseCommand cmd)
{
    base.qobject->setCommandAll2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll3(MouseCommand cmd)
{
    base.qobject->setCommandAll3(static_cast<win::mouse_cmd>(cmd));
}

void options::setKeyCmdAllModKey(uint keyCmdAllModKey)
{
    base.qobject->setKeyCmdAllModKey(keyCmdAllModKey);
}

void options::setCondensedTitle(bool condensedTitle)
{
    base.qobject->setCondensedTitle(condensedTitle);
}

void options::setElectricBorderMaximize(bool electricBorderMaximize)
{
    base.qobject->setElectricBorderMaximize(electricBorderMaximize);
}

void options::setElectricBorderTiling(bool electricBorderTiling)
{
    base.qobject->setElectricBorderTiling(electricBorderTiling);
}

void options::setElectricBorderCornerRatio(float electricBorderCornerRatio)
{
    base.qobject->setElectricBorderCornerRatio(electricBorderCornerRatio);
}

void options::setBorderlessMaximizedWindows(bool borderlessMaximizedWindows)
{
    base.qobject->setBorderlessMaximizedWindows(borderlessMaximizedWindows);
}

void options::setKillPingTimeout(int killPingTimeout)
{
    base.qobject->setKillPingTimeout(killPingTimeout);
}

void options::setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive)
{
    base.qobject->setHideUtilityWindowsForInactive(hideUtilityWindowsForInactive);
}

void options::setUseCompositing(bool useCompositing)
{
    base.qobject->setUseCompositing(useCompositing);
}

void options::setHiddenPreviews(int set)
{
    base.qobject->setHiddenPreviews(static_cast<render::x11::hidden_preview>(set));
}

void options::setMaxFpsInterval(qint64 maxFpsInterval)
{
    base.qobject->setMaxFpsInterval(maxFpsInterval);
}

void options::setRefreshRate(uint refreshRate)
{
    base.qobject->setRefreshRate(refreshRate);
}

void options::setVBlankTime(qint64 vBlankTime)
{
    base.qobject->setVBlankTime(vBlankTime);
}

void options::setGlStrictBinding(bool glStrictBinding)
{
    base.qobject->setGlStrictBinding(glStrictBinding);
}

void options::setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver)
{
    base.qobject->setGlStrictBindingFollowsDriver(glStrictBindingFollowsDriver);
}

void options::setWindowsBlockCompositing(bool set)
{
    base.qobject->setWindowsBlockCompositing(set);
}

void options::setAnimationCurve(AnimationCurve curve)
{
    base.qobject->setAnimationCurve(static_cast<render::animation_curve>(curve));
}

}
