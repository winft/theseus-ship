/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "base/options.h"
#include "render/options.h"
#include "win/options.h"

namespace KWin::scripting
{

enum CompositingType {
    NoCompositing = 0,
    OpenGLCompositing = 1,
    /*XRenderCompositing = 2,*/
    QPainterCompositing = 3,
};

options::options(base::options& base, win::options& win, render::options& render)
    : base{base}
    , win{win}
    , render{render}
{
    auto qbase = base.qobject.get();
    auto qwin = win.qobject.get();
    auto qrender = render.qobject.get();

    QObject::connect(
        qwin, &win::options_qobject::focusPolicyChanged, this, &options::focusPolicyChanged);
    QObject::connect(qwin,
                     &win::options_qobject::focusPolicyIsResonableChanged,
                     this,
                     &options::focusPolicyIsResonableChanged);
    QObject::connect(qwin,
                     &win::options_qobject::nextFocusPrefersMouseChanged,
                     this,
                     &options::nextFocusPrefersMouseChanged);

    QObject::connect(
        qwin, &win::options_qobject::clickRaiseChanged, this, &options::clickRaiseChanged);
    QObject::connect(
        qwin, &win::options_qobject::autoRaiseChanged, this, &options::autoRaiseChanged);
    QObject::connect(qwin,
                     &win::options_qobject::autoRaiseIntervalChanged,
                     this,
                     &options::autoRaiseIntervalChanged);
    QObject::connect(qwin,
                     &win::options_qobject::delayFocusIntervalChanged,
                     this,
                     &options::delayFocusIntervalChanged);
    QObject::connect(qwin,
                     &win::options_qobject::separateScreenFocusChanged,
                     this,
                     &options::separateScreenFocusChanged);

    QObject::connect(
        qwin, &win::options_qobject::placementChanged, this, &options::placementChanged);
    QObject::connect(
        qwin, &win::options_qobject::borderSnapZoneChanged, this, &options::borderSnapZoneChanged);
    QObject::connect(
        qwin, &win::options_qobject::windowSnapZoneChanged, this, &options::windowSnapZoneChanged);
    QObject::connect(
        qwin, &win::options_qobject::centerSnapZoneChanged, this, &options::centerSnapZoneChanged);
    QObject::connect(qwin,
                     &win::options_qobject::snapOnlyWhenOverlappingChanged,
                     this,
                     &options::snapOnlyWhenOverlappingChanged);

    QObject::connect(qwin,
                     &win::options_qobject::rollOverDesktopsChanged,
                     this,
                     &options::rollOverDesktopsChanged);
    QObject::connect(qwin,
                     &win::options_qobject::focusStealingPreventionLevelChanged,
                     this,
                     &options::focusStealingPreventionLevelChanged);

    QObject::connect(qwin,
                     &win::options_qobject::operationTitlebarDblClickChanged,
                     this,
                     &options::operationTitlebarDblClickChanged);
    QObject::connect(qwin,
                     &win::options_qobject::operationMaxButtonLeftClickChanged,
                     this,
                     &options::operationMaxButtonLeftClickChanged);
    QObject::connect(qwin,
                     &win::options_qobject::operationMaxButtonRightClickChanged,
                     this,
                     &options::operationMaxButtonRightClickChanged);
    QObject::connect(qwin,
                     &win::options_qobject::operationMaxButtonMiddleClickChanged,
                     this,
                     &options::operationMaxButtonMiddleClickChanged);

    QObject::connect(qwin,
                     &win::options_qobject::commandActiveTitlebar1Changed,
                     this,
                     &options::commandActiveTitlebar1Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandActiveTitlebar2Changed,
                     this,
                     &options::commandActiveTitlebar2Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandActiveTitlebar3Changed,
                     this,
                     &options::commandActiveTitlebar3Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandInactiveTitlebar1Changed,
                     this,
                     &options::commandInactiveTitlebar1Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandInactiveTitlebar2Changed,
                     this,
                     &options::commandInactiveTitlebar2Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandInactiveTitlebar3Changed,
                     this,
                     &options::commandInactiveTitlebar3Changed);

    QObject::connect(
        qwin, &win::options_qobject::commandWindow1Changed, this, &options::commandWindow1Changed);
    QObject::connect(
        qwin, &win::options_qobject::commandWindow2Changed, this, &options::commandWindow2Changed);
    QObject::connect(
        qwin, &win::options_qobject::commandWindow3Changed, this, &options::commandWindow3Changed);
    QObject::connect(qwin,
                     &win::options_qobject::commandWindowWheelChanged,
                     this,
                     &options::commandWindowWheelChanged);

    QObject::connect(
        qwin, &win::options_qobject::commandAll1Changed, this, &options::commandAll1Changed);
    QObject::connect(
        qwin, &win::options_qobject::commandAll2Changed, this, &options::commandAll2Changed);
    QObject::connect(
        qwin, &win::options_qobject::commandAll3Changed, this, &options::commandAll3Changed);
    QObject::connect(qwin,
                     &win::options_qobject::keyCmdAllModKeyChanged,
                     this,
                     &options::keyCmdAllModKeyChanged);

    QObject::connect(
        qwin, &win::options_qobject::condensedTitleChanged, this, &options::condensedTitleChanged);

    QObject::connect(qwin,
                     &win::options_qobject::electricBorderMaximizeChanged,
                     this,
                     &options::electricBorderMaximizeChanged);
    QObject::connect(qwin,
                     &win::options_qobject::electricBorderTilingChanged,
                     this,
                     &options::electricBorderTilingChanged);
    QObject::connect(qwin,
                     &win::options_qobject::electricBorderCornerRatioChanged,
                     this,
                     &options::electricBorderCornerRatioChanged);

    QObject::connect(qwin,
                     &win::options_qobject::borderlessMaximizedWindowsChanged,
                     this,
                     &options::borderlessMaximizedWindowsChanged);
    QObject::connect(qwin,
                     &win::options_qobject::killPingTimeoutChanged,
                     this,
                     &options::killPingTimeoutChanged);
    QObject::connect(qwin,
                     &win::options_qobject::hideUtilityWindowsForInactiveChanged,
                     this,
                     &options::hideUtilityWindowsForInactiveChanged);
    QObject::connect(qrender,
                     &render::options_qobject::sw_compositing_changed,
                     this,
                     &options::compositingModeChanged);
    QObject::connect(qrender,
                     &render::options_qobject::useCompositingChanged,
                     this,
                     &options::useCompositingChanged);
    QObject::connect(qrender,
                     &render::options_qobject::hiddenPreviewsChanged,
                     this,
                     &options::hiddenPreviewsChanged);

    QObject::connect(qrender,
                     &render::options_qobject::maxFpsIntervalChanged,
                     this,
                     &options::maxFpsIntervalChanged);
    QObject::connect(
        qrender, &render::options_qobject::refreshRateChanged, this, &options::refreshRateChanged);
    QObject::connect(
        qrender, &render::options_qobject::vBlankTimeChanged, this, &options::vBlankTimeChanged);
    QObject::connect(qrender,
                     &render::options_qobject::glStrictBindingChanged,
                     this,
                     &options::glStrictBindingChanged);
    QObject::connect(qrender,
                     &render::options_qobject::glStrictBindingFollowsDriverChanged,
                     this,
                     &options::glStrictBindingFollowsDriverChanged);
    QObject::connect(qrender,
                     &render::options_qobject::windowsBlockCompositingChanged,
                     this,
                     &options::windowsBlockCompositingChanged);

    QObject::connect(qrender,
                     &render::options_qobject::animationSpeedChanged,
                     this,
                     &options::animationSpeedChanged);
    QObject::connect(qrender,
                     &render::options_qobject::animationCurveChanged,
                     this,
                     &options::animationCurveChanged);

    QObject::connect(qbase, &base::options_qobject::configChanged, this, &options::configChanged);
    QObject::connect(qwin, &win::options_qobject::configChanged, this, &options::configChanged);
    QObject::connect(
        qrender, &render::options_qobject::configChanged, this, &options::configChanged);
}

options::FocusPolicy options::focusPolicy() const
{
    return static_cast<options::FocusPolicy>(win.qobject->focusPolicy());
}

bool options::isNextFocusPrefersMouse() const
{
    return win.qobject->isNextFocusPrefersMouse();
}

bool options::isClickRaise() const
{
    return win.qobject->isClickRaise();
}

bool options::isAutoRaise() const
{
    return win.qobject->isAutoRaise();
}

int options::autoRaiseInterval() const
{
    return win.qobject->autoRaiseInterval();
}

int options::delayFocusInterval() const
{
    return win.qobject->delayFocusInterval();
}

bool options::isSeparateScreenFocus() const
{
    return win.qobject->isSeparateScreenFocus();
}

win::placement options::placement() const
{
    return win.qobject->placement();
}

bool options::focusPolicyIsReasonable()
{
    return win.qobject->focusPolicyIsReasonable();
}

int options::borderSnapZone() const
{
    return win.qobject->borderSnapZone();
}

int options::windowSnapZone() const
{
    return win.qobject->windowSnapZone();
}

int options::centerSnapZone() const
{
    return win.qobject->centerSnapZone();
}

bool options::isSnapOnlyWhenOverlapping() const
{
    return win.qobject->isSnapOnlyWhenOverlapping();
}

bool options::isRollOverDesktops() const
{
    return win.qobject->isRollOverDesktops();
}

win::fsp_level options::focusStealingPreventionLevel() const
{
    return win.qobject->focusStealingPreventionLevel();
}

options::WindowOperation options::operationTitlebarDblClick() const
{
    return static_cast<options::WindowOperation>(win.qobject->operationTitlebarDblClick());
}

options::WindowOperation options::operationMaxButtonLeftClick() const
{
    return static_cast<options::WindowOperation>(win.qobject->operationMaxButtonLeftClick());
}

options::WindowOperation options::operationMaxButtonRightClick() const
{
    return static_cast<options::WindowOperation>(win.qobject->operationMaxButtonRightClick());
}

options::WindowOperation options::operationMaxButtonMiddleClick() const
{
    return static_cast<options::WindowOperation>(win.qobject->operationMaxButtonMiddleClick());
}

options::WindowOperation options::operationMaxButtonClick(Qt::MouseButtons button) const
{
    return static_cast<options::WindowOperation>(win.qobject->operationMaxButtonClick(button));
}

options::MouseCommand options::commandActiveTitlebar1() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandActiveTitlebar1());
}

options::MouseCommand options::commandActiveTitlebar2() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandActiveTitlebar2());
}

options::MouseCommand options::commandActiveTitlebar3() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandActiveTitlebar3());
}

options::MouseCommand options::commandInactiveTitlebar1() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandInactiveTitlebar1());
}

options::MouseCommand options::commandInactiveTitlebar2() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandInactiveTitlebar2());
}

options::MouseCommand options::commandInactiveTitlebar3() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandInactiveTitlebar3());
}

options::MouseCommand options::commandWindow1() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandWindow1());
}

options::MouseCommand options::commandWindow2() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandWindow2());
}

options::MouseCommand options::commandWindow3() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandWindow3());
}

options::MouseCommand options::commandWindowWheel() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandWindowWheel());
}

options::MouseCommand options::commandAll1() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandAll1());
}

options::MouseCommand options::commandAll2() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandAll2());
}
options::MouseCommand options::commandAll3() const
{
    return static_cast<options::MouseCommand>(win.qobject->commandAll3());
}

options::MouseWheelCommand options::commandAllWheel() const
{
    return static_cast<options::MouseWheelCommand>(win.qobject->commandAllWheel());
}

uint options::keyCmdAllModKey() const
{
    return win.qobject->keyCmdAllModKey();
}

Qt::KeyboardModifier options::commandAllModifier() const
{
    return win.qobject->commandAllModifier();
}

bool options::condensedTitle() const
{
    return win.qobject->condensedTitle();
}

bool options::electricBorderMaximize() const
{
    return win.qobject->electricBorderMaximize();
}

bool options::electricBorderTiling() const
{
    return win.qobject->electricBorderTiling();
}

float options::electricBorderCornerRatio() const
{
    return win.qobject->electricBorderCornerRatio();
}

bool options::borderlessMaximizedWindows() const
{
    return win.qobject->borderlessMaximizedWindows();
}

int options::killPingTimeout() const
{
    return win.qobject->killPingTimeout();
}

bool options::isHideUtilityWindowsForInactive() const
{
    return win.qobject->isHideUtilityWindowsForInactive();
}

int options::compositingMode() const
{
    return render.qobject->sw_compositing() ? QPainterCompositing : OpenGLCompositing;
}

void options::setCompositingMode(int mode)
{
    render.qobject->set_sw_compositing(mode == QPainterCompositing);
}

bool options::isUseCompositing() const
{
    return render.qobject->isUseCompositing();
}

int options::hiddenPreviews() const
{
    return static_cast<int>(render.qobject->hiddenPreviews());
}

qint64 options::maxFpsInterval() const
{
    return render.qobject->maxFpsInterval();
}

uint options::refreshRate() const
{
    return render.qobject->refreshRate();
}

qint64 options::vBlankTime() const
{
    return render.qobject->vBlankTime();
}

bool options::isGlStrictBinding() const
{
    return render.qobject->isGlStrictBinding();
}

bool options::isGlStrictBindingFollowsDriver() const
{
    return render.qobject->isGlStrictBindingFollowsDriver();
}

bool options::windowsBlockCompositing() const
{
    return render.qobject->windowsBlockCompositing();
}

options::AnimationCurve options::animationCurve() const
{
    return static_cast<options::AnimationCurve>(render.qobject->animationCurve());
}

void options::setFocusPolicy(options::FocusPolicy focusPolicy)
{
    win.qobject->setFocusPolicy(static_cast<win::focus_policy>(focusPolicy));
}

void options::setNextFocusPrefersMouse(bool nextFocusPrefersMouse)
{
    win.qobject->setNextFocusPrefersMouse(nextFocusPrefersMouse);
}

void options::setClickRaise(bool clickRaise)
{
    win.qobject->setClickRaise(clickRaise);
}

void options::setAutoRaise(bool autoRaise)
{
    win.qobject->setAutoRaise(autoRaise);
}

void options::setAutoRaiseInterval(int autoRaiseInterval)
{
    win.qobject->setAutoRaiseInterval(autoRaiseInterval);
}

void options::setDelayFocusInterval(int delayFocusInterval)
{
    win.qobject->setDelayFocusInterval(delayFocusInterval);
}

void options::setSeparateScreenFocus(bool separateScreenFocus)
{
    win.qobject->setSeparateScreenFocus(separateScreenFocus);
}

void options::setPlacement(win::placement placement)
{
    win.qobject->setPlacement(placement);
}

void options::setBorderSnapZone(int borderSnapZone)
{
    win.qobject->setBorderSnapZone(borderSnapZone);
}

void options::setWindowSnapZone(int windowSnapZone)
{
    win.qobject->setWindowSnapZone(windowSnapZone);
}

void options::setCenterSnapZone(int centerSnapZone)
{
    win.qobject->setCenterSnapZone(centerSnapZone);
}

void options::setSnapOnlyWhenOverlapping(bool snapOnlyWhenOverlapping)
{
    win.qobject->setSnapOnlyWhenOverlapping(snapOnlyWhenOverlapping);
}

void options::setRollOverDesktops(bool rollOverDesktops)
{
    win.qobject->setRollOverDesktops(rollOverDesktops);
}

void options::setFocusStealingPreventionLevel(win::fsp_level lvl)
{
    win.qobject->setFocusStealingPreventionLevel(lvl);
}

void options::setOperationTitlebarDblClick(WindowOperation op)
{
    win.qobject->setOperationTitlebarDblClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonLeftClick(WindowOperation op)
{
    win.qobject->setOperationMaxButtonLeftClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonRightClick(WindowOperation op)
{
    win.qobject->setOperationMaxButtonRightClick(static_cast<win::win_op>(op));
}

void options::setOperationMaxButtonMiddleClick(WindowOperation op)
{
    win.qobject->setOperationMaxButtonMiddleClick(static_cast<win::win_op>(op));
}

void options::setCommandActiveTitlebar1(MouseCommand cmd)
{
    win.qobject->setCommandActiveTitlebar1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandActiveTitlebar2(MouseCommand cmd)
{
    win.qobject->setCommandActiveTitlebar2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandActiveTitlebar3(MouseCommand cmd)
{
    win.qobject->setCommandActiveTitlebar3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar1(MouseCommand cmd)
{
    win.qobject->setCommandInactiveTitlebar1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar2(MouseCommand cmd)
{
    win.qobject->setCommandInactiveTitlebar2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandInactiveTitlebar3(MouseCommand cmd)
{
    win.qobject->setCommandInactiveTitlebar3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow1(MouseCommand cmd)
{
    win.qobject->setCommandWindow1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow2(MouseCommand cmd)
{
    win.qobject->setCommandWindow2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindow3(MouseCommand cmd)
{
    win.qobject->setCommandWindow3(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandWindowWheel(MouseCommand cmd)
{
    win.qobject->setCommandWindowWheel(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll1(MouseCommand cmd)
{
    win.qobject->setCommandAll1(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll2(MouseCommand cmd)
{
    win.qobject->setCommandAll2(static_cast<win::mouse_cmd>(cmd));
}

void options::setCommandAll3(MouseCommand cmd)
{
    win.qobject->setCommandAll3(static_cast<win::mouse_cmd>(cmd));
}

void options::setKeyCmdAllModKey(uint keyCmdAllModKey)
{
    win.qobject->setKeyCmdAllModKey(keyCmdAllModKey);
}

void options::setCondensedTitle(bool condensedTitle)
{
    win.qobject->setCondensedTitle(condensedTitle);
}

void options::setElectricBorderMaximize(bool electricBorderMaximize)
{
    win.qobject->setElectricBorderMaximize(electricBorderMaximize);
}

void options::setElectricBorderTiling(bool electricBorderTiling)
{
    win.qobject->setElectricBorderTiling(electricBorderTiling);
}

void options::setElectricBorderCornerRatio(float electricBorderCornerRatio)
{
    win.qobject->setElectricBorderCornerRatio(electricBorderCornerRatio);
}

void options::setBorderlessMaximizedWindows(bool borderlessMaximizedWindows)
{
    win.qobject->setBorderlessMaximizedWindows(borderlessMaximizedWindows);
}

void options::setKillPingTimeout(int killPingTimeout)
{
    win.qobject->setKillPingTimeout(killPingTimeout);
}

void options::setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive)
{
    win.qobject->setHideUtilityWindowsForInactive(hideUtilityWindowsForInactive);
}

void options::setUseCompositing(bool useCompositing)
{
    render.qobject->setUseCompositing(useCompositing);
}

void options::setHiddenPreviews(int set)
{
    render.qobject->setHiddenPreviews(static_cast<render::x11::hidden_preview>(set));
}

void options::setMaxFpsInterval(qint64 maxFpsInterval)
{
    render.qobject->setMaxFpsInterval(maxFpsInterval);
}

void options::setRefreshRate(uint refreshRate)
{
    render.qobject->setRefreshRate(refreshRate);
}

void options::setVBlankTime(qint64 vBlankTime)
{
    render.qobject->setVBlankTime(vBlankTime);
}

void options::setGlStrictBinding(bool glStrictBinding)
{
    render.qobject->setGlStrictBinding(glStrictBinding);
}

void options::setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver)
{
    render.qobject->setGlStrictBindingFollowsDriver(glStrictBindingFollowsDriver);
}

void options::setWindowsBlockCompositing(bool set)
{
    render.qobject->setWindowsBlockCompositing(set);
}

void options::setAnimationCurve(AnimationCurve curve)
{
    render.qobject->setAnimationCurve(static_cast<render::animation_curve>(curve));
}

}
