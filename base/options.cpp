/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "logging.h"
#include "platform.h"

#include "config-kwin.h"
#include "render/platform.h"

#ifndef KCMRULES
#include "main.h"
#include "options_settings.h"
#include "utils/algorithm.h"

#include <kwingl/platform.h>

#include <QOpenGLContext>
#include <QProcess>
#endif

namespace KWin::base
{

#ifndef KCMRULES

OpenGLPlatformInterface defaultGlPlatformInterface()
{
    return kwinApp()->shouldUseWaylandForCompositing() ? EglPlatformInterface
                                                       : GlxPlatformInterface;
}

void options_qobject::setFocusPolicy(FocusPolicy focusPolicy)
{
    if (m_focusPolicy == focusPolicy) {
        return;
    }
    m_focusPolicy = focusPolicy;
    Q_EMIT focusPolicyChanged();
    if (m_focusPolicy == ClickToFocus) {
        setAutoRaise(false);
        setAutoRaiseInterval(0);
        setDelayFocusInterval(0);
    }
}

bool options::get_current_output_follows_mouse() const
{
    return current_output_follows_mouse;
}

void options_qobject::setNextFocusPrefersMouse(bool nextFocusPrefersMouse)
{
    if (m_nextFocusPrefersMouse == nextFocusPrefersMouse) {
        return;
    }
    m_nextFocusPrefersMouse = nextFocusPrefersMouse;
    Q_EMIT nextFocusPrefersMouseChanged();
}

void options_qobject::setClickRaise(bool clickRaise)
{
    if (m_autoRaise) {
        // important: autoRaise implies ClickRaise
        clickRaise = true;
    }
    if (m_clickRaise == clickRaise) {
        return;
    }
    m_clickRaise = clickRaise;
    Q_EMIT clickRaiseChanged();
}

void options_qobject::setAutoRaise(bool autoRaise)
{
    if (m_focusPolicy == ClickToFocus) {
        autoRaise = false;
    }
    if (m_autoRaise == autoRaise) {
        return;
    }
    m_autoRaise = autoRaise;
    if (m_autoRaise) {
        // important: autoRaise implies ClickRaise
        setClickRaise(true);
    }
    Q_EMIT autoRaiseChanged();
}

void options_qobject::setAutoRaiseInterval(int autoRaiseInterval)
{
    if (m_focusPolicy == ClickToFocus) {
        autoRaiseInterval = 0;
    }
    if (m_autoRaiseInterval == autoRaiseInterval) {
        return;
    }
    m_autoRaiseInterval = autoRaiseInterval;
    Q_EMIT autoRaiseIntervalChanged();
}

void options_qobject::setDelayFocusInterval(int delayFocusInterval)
{
    if (m_focusPolicy == ClickToFocus) {
        delayFocusInterval = 0;
    }
    if (m_delayFocusInterval == delayFocusInterval) {
        return;
    }
    m_delayFocusInterval = delayFocusInterval;
    Q_EMIT delayFocusIntervalChanged();
}

void options_qobject::setSeparateScreenFocus(bool separateScreenFocus)
{
    if (m_separateScreenFocus == separateScreenFocus) {
        return;
    }
    m_separateScreenFocus = separateScreenFocus;
    Q_EMIT separateScreenFocusChanged(m_separateScreenFocus);
}

void options_qobject::setPlacement(win::placement placement)
{
    if (m_placement == placement) {
        return;
    }
    m_placement = placement;
    Q_EMIT placementChanged();
}

void options_qobject::setBorderSnapZone(int borderSnapZone)
{
    if (m_borderSnapZone == borderSnapZone) {
        return;
    }
    m_borderSnapZone = borderSnapZone;
    Q_EMIT borderSnapZoneChanged();
}

void options_qobject::setWindowSnapZone(int windowSnapZone)
{
    if (m_windowSnapZone == windowSnapZone) {
        return;
    }
    m_windowSnapZone = windowSnapZone;
    Q_EMIT windowSnapZoneChanged();
}

void options_qobject::setCenterSnapZone(int centerSnapZone)
{
    if (m_centerSnapZone == centerSnapZone) {
        return;
    }
    m_centerSnapZone = centerSnapZone;
    Q_EMIT centerSnapZoneChanged();
}

void options_qobject::setSnapOnlyWhenOverlapping(bool snapOnlyWhenOverlapping)
{
    if (m_snapOnlyWhenOverlapping == snapOnlyWhenOverlapping) {
        return;
    }
    m_snapOnlyWhenOverlapping = snapOnlyWhenOverlapping;
    Q_EMIT snapOnlyWhenOverlappingChanged();
}

void options_qobject::setRollOverDesktops(bool rollOverDesktops)
{
    if (m_rollOverDesktops == rollOverDesktops) {
        return;
    }
    m_rollOverDesktops = rollOverDesktops;
    Q_EMIT rollOverDesktopsChanged(m_rollOverDesktops);
}

void options_qobject::setFocusStealingPreventionLevel(win::fsp_level focusStealingPreventionLevel)
{
    if (!focusPolicyIsReasonable()) {
        focusStealingPreventionLevel = win::fsp_level::none;
    }
    if (m_focusStealingPreventionLevel == focusStealingPreventionLevel) {
        return;
    }

    if (enum_index(focusStealingPreventionLevel) > enum_index(win::fsp_level::extreme)) {
        focusStealingPreventionLevel = win::fsp_level::extreme;
    }
    if (enum_index(focusStealingPreventionLevel) < enum_index(win::fsp_level::none)) {
        focusStealingPreventionLevel = win::fsp_level::none;
    }

    m_focusStealingPreventionLevel = focusStealingPreventionLevel;
    Q_EMIT focusStealingPreventionLevelChanged();
}

void options_qobject::setOperationTitlebarDblClick(WindowOperation operationTitlebarDblClick)
{
    if (OpTitlebarDblClick == operationTitlebarDblClick) {
        return;
    }
    OpTitlebarDblClick = operationTitlebarDblClick;
    Q_EMIT operationTitlebarDblClickChanged();
}

void options_qobject::setOperationMaxButtonLeftClick(WindowOperation op)
{
    if (opMaxButtonLeftClick == op) {
        return;
    }
    opMaxButtonLeftClick = op;
    Q_EMIT operationMaxButtonLeftClickChanged();
}

void options_qobject::setOperationMaxButtonRightClick(WindowOperation op)
{
    if (opMaxButtonRightClick == op) {
        return;
    }
    opMaxButtonRightClick = op;
    Q_EMIT operationMaxButtonRightClickChanged();
}

void options_qobject::setOperationMaxButtonMiddleClick(WindowOperation op)
{
    if (opMaxButtonMiddleClick == op) {
        return;
    }
    opMaxButtonMiddleClick = op;
    Q_EMIT operationMaxButtonMiddleClickChanged();
}

void options_qobject::setCommandActiveTitlebar1(MouseCommand commandActiveTitlebar1)
{
    if (CmdActiveTitlebar1 == commandActiveTitlebar1) {
        return;
    }
    CmdActiveTitlebar1 = commandActiveTitlebar1;
    Q_EMIT commandActiveTitlebar1Changed();
}

void options_qobject::setCommandActiveTitlebar2(MouseCommand commandActiveTitlebar2)
{
    if (CmdActiveTitlebar2 == commandActiveTitlebar2) {
        return;
    }
    CmdActiveTitlebar2 = commandActiveTitlebar2;
    Q_EMIT commandActiveTitlebar2Changed();
}

void options_qobject::setCommandActiveTitlebar3(MouseCommand commandActiveTitlebar3)
{
    if (CmdActiveTitlebar3 == commandActiveTitlebar3) {
        return;
    }
    CmdActiveTitlebar3 = commandActiveTitlebar3;
    Q_EMIT commandActiveTitlebar3Changed();
}

void options_qobject::setCommandInactiveTitlebar1(MouseCommand commandInactiveTitlebar1)
{
    if (CmdInactiveTitlebar1 == commandInactiveTitlebar1) {
        return;
    }
    CmdInactiveTitlebar1 = commandInactiveTitlebar1;
    Q_EMIT commandInactiveTitlebar1Changed();
}

void options_qobject::setCommandInactiveTitlebar2(MouseCommand commandInactiveTitlebar2)
{
    if (CmdInactiveTitlebar2 == commandInactiveTitlebar2) {
        return;
    }
    CmdInactiveTitlebar2 = commandInactiveTitlebar2;
    Q_EMIT commandInactiveTitlebar2Changed();
}

void options_qobject::setCommandInactiveTitlebar3(MouseCommand commandInactiveTitlebar3)
{
    if (CmdInactiveTitlebar3 == commandInactiveTitlebar3) {
        return;
    }
    CmdInactiveTitlebar3 = commandInactiveTitlebar3;
    Q_EMIT commandInactiveTitlebar3Changed();
}

void options_qobject::setCommandWindow1(MouseCommand commandWindow1)
{
    if (CmdWindow1 == commandWindow1) {
        return;
    }
    CmdWindow1 = commandWindow1;
    Q_EMIT commandWindow1Changed();
}

void options_qobject::setCommandWindow2(MouseCommand commandWindow2)
{
    if (CmdWindow2 == commandWindow2) {
        return;
    }
    CmdWindow2 = commandWindow2;
    Q_EMIT commandWindow2Changed();
}

void options_qobject::setCommandWindow3(MouseCommand commandWindow3)
{
    if (CmdWindow3 == commandWindow3) {
        return;
    }
    CmdWindow3 = commandWindow3;
    Q_EMIT commandWindow3Changed();
}

void options_qobject::setCommandWindowWheel(MouseCommand commandWindowWheel)
{
    if (CmdWindowWheel == commandWindowWheel) {
        return;
    }
    CmdWindowWheel = commandWindowWheel;
    Q_EMIT commandWindowWheelChanged();
}

void options_qobject::setCommandAll1(MouseCommand commandAll1)
{
    if (CmdAll1 == commandAll1) {
        return;
    }
    CmdAll1 = commandAll1;
    Q_EMIT commandAll1Changed();
}

void options_qobject::setCommandAll2(MouseCommand commandAll2)
{
    if (CmdAll2 == commandAll2) {
        return;
    }
    CmdAll2 = commandAll2;
    Q_EMIT commandAll2Changed();
}

void options_qobject::setCommandAll3(MouseCommand commandAll3)
{
    if (CmdAll3 == commandAll3) {
        return;
    }
    CmdAll3 = commandAll3;
    Q_EMIT commandAll3Changed();
}

void options_qobject::setKeyCmdAllModKey(uint keyCmdAllModKey)
{
    if (CmdAllModKey == keyCmdAllModKey) {
        return;
    }
    CmdAllModKey = keyCmdAllModKey;
    Q_EMIT keyCmdAllModKeyChanged();
}

void options_qobject::setCondensedTitle(bool condensedTitle)
{
    if (condensed_title == condensedTitle) {
        return;
    }
    condensed_title = condensedTitle;
    Q_EMIT condensedTitleChanged();
}

void options_qobject::setElectricBorderMaximize(bool electricBorderMaximize)
{
    if (electric_border_maximize == electricBorderMaximize) {
        return;
    }
    electric_border_maximize = electricBorderMaximize;
    Q_EMIT electricBorderMaximizeChanged();
}

void options_qobject::setElectricBorderTiling(bool electricBorderTiling)
{
    if (electric_border_tiling == electricBorderTiling) {
        return;
    }
    electric_border_tiling = electricBorderTiling;
    Q_EMIT electricBorderTilingChanged();
}

void options_qobject::setElectricBorderCornerRatio(float electricBorderCornerRatio)
{
    if (electric_border_corner_ratio == electricBorderCornerRatio) {
        return;
    }
    electric_border_corner_ratio = electricBorderCornerRatio;
    Q_EMIT electricBorderCornerRatioChanged();
}

void options_qobject::setBorderlessMaximizedWindows(bool borderlessMaximizedWindows)
{
    if (borderless_maximized_windows == borderlessMaximizedWindows) {
        return;
    }
    borderless_maximized_windows = borderlessMaximizedWindows;
    Q_EMIT borderlessMaximizedWindowsChanged();
}

void options_qobject::setKillPingTimeout(int killPingTimeout)
{
    if (m_killPingTimeout == killPingTimeout) {
        return;
    }
    m_killPingTimeout = killPingTimeout;
    Q_EMIT killPingTimeoutChanged();
}

void options_qobject::setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive)
{
    if (m_hideUtilityWindowsForInactive == hideUtilityWindowsForInactive) {
        return;
    }
    m_hideUtilityWindowsForInactive = hideUtilityWindowsForInactive;
    Q_EMIT hideUtilityWindowsForInactiveChanged();
}

void options_qobject::setCompositingMode(int compositingMode)
{
    if (m_compositingMode == static_cast<CompositingType>(compositingMode)) {
        return;
    }
    m_compositingMode = static_cast<CompositingType>(compositingMode);
    Q_EMIT compositingModeChanged();
}

void options_qobject::setUseCompositing(bool useCompositing)
{
    if (m_useCompositing == useCompositing) {
        return;
    }
    m_useCompositing = useCompositing;
    Q_EMIT useCompositingChanged();
}

void options_qobject::setHiddenPreviews(int hiddenPreviews)
{
    if (m_hiddenPreviews == static_cast<HiddenPreviews>(hiddenPreviews)) {
        return;
    }
    m_hiddenPreviews = static_cast<HiddenPreviews>(hiddenPreviews);
    Q_EMIT hiddenPreviewsChanged();
}

void options_qobject::setMaxFpsInterval(qint64 maxFpsInterval)
{
    if (m_maxFpsInterval == maxFpsInterval) {
        return;
    }
    m_maxFpsInterval = maxFpsInterval;
    Q_EMIT maxFpsIntervalChanged();
}

void options_qobject::setRefreshRate(uint refreshRate)
{
    if (m_refreshRate == refreshRate) {
        return;
    }
    m_refreshRate = refreshRate;
    Q_EMIT refreshRateChanged();
}

void options_qobject::setVBlankTime(qint64 vBlankTime)
{
    if (m_vBlankTime == vBlankTime) {
        return;
    }
    m_vBlankTime = vBlankTime;
    Q_EMIT vBlankTimeChanged();
}

void options_qobject::setGlStrictBinding(bool glStrictBinding)
{
    if (m_glStrictBinding == glStrictBinding) {
        return;
    }
    m_glStrictBinding = glStrictBinding;
    Q_EMIT glStrictBindingChanged();
}

void options_qobject::setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver)
{
    if (m_glStrictBindingFollowsDriver == glStrictBindingFollowsDriver) {
        return;
    }
    m_glStrictBindingFollowsDriver = glStrictBindingFollowsDriver;
    Q_EMIT glStrictBindingFollowsDriverChanged();
}

void options_qobject::setWindowsBlockCompositing(bool value)
{
    if (m_windowsBlockCompositing == value) {
        return;
    }
    m_windowsBlockCompositing = value;
    Q_EMIT windowsBlockCompositingChanged();
}

void options_qobject::setAnimationCurve(AnimationCurve curve)
{
    if (m_animationCurve == curve) {
        return;
    }

    qCDebug(KWIN_CORE) << "Setting animation curve: " << curve;
    m_animationCurve = curve;
    Q_EMIT animationCurveChanged();
}

void options::updateSettings()
{
    loadConfig();
    // Read button tooltip animation effect from kdeglobals
    // Since we want to allow users to enable window decoration tooltips
    // and not kstyle tooltips and vise-versa, we don't read the
    // "EffectNoTooltip" setting from kdeglobals.

    //    QToolTip::setGloballyEnabled( d->show_tooltips );
    // KDE4 this probably needs to be done manually in clients

    // Driver-specific config detection
    reloadCompositingSettings();

    Q_EMIT qobject->configChanged();
}

void options::loadConfig()
{
    m_settings->load();

    syncFromKcfgc();

    // Electric borders
    KConfigGroup config(m_settings->config(), "Windows");
    qobject->OpTitlebarDblClick
        = windowOperation(config.readEntry("TitlebarDoubleClickCommand", "Maximize"), true);
    qobject->setOperationMaxButtonLeftClick(
        windowOperation(config.readEntry("MaximizeButtonLeftClickCommand", "Maximize"), true));
    qobject->setOperationMaxButtonMiddleClick(windowOperation(
        config.readEntry("MaximizeButtonMiddleClickCommand", "Maximize (vertical only)"), true));
    qobject->setOperationMaxButtonRightClick(windowOperation(
        config.readEntry("MaximizeButtonRightClickCommand", "Maximize (horizontal only)"), true));

    // Mouse bindings
    config = KConfigGroup(m_settings->config(), "MouseBindings");
    // TODO: add properties for missing options
    qobject->CmdTitlebarWheel
        = mouseWheelCommand(config.readEntry("CommandTitlebarWheel", "Nothing"));
    qobject->CmdAllModKey = (config.readEntry("CommandAllKey", "Meta") == QStringLiteral("Meta"))
        ? Qt::Key_Meta
        : Qt::Key_Alt;
    qobject->CmdAllWheel = mouseWheelCommand(config.readEntry("CommandAllWheel", "Nothing"));
    qobject->setCommandActiveTitlebar1(
        mouseCommand(config.readEntry("CommandActiveTitlebar1", "Raise"), true));
    qobject->setCommandActiveTitlebar2(
        mouseCommand(config.readEntry("CommandActiveTitlebar2", "Nothing"), true));
    qobject->setCommandActiveTitlebar3(
        mouseCommand(config.readEntry("CommandActiveTitlebar3", "Operations menu"), true));
    qobject->setCommandInactiveTitlebar1(
        mouseCommand(config.readEntry("CommandInactiveTitlebar1", "Activate and raise"), true));
    qobject->setCommandInactiveTitlebar2(
        mouseCommand(config.readEntry("CommandInactiveTitlebar2", "Nothing"), true));
    qobject->setCommandInactiveTitlebar3(
        mouseCommand(config.readEntry("CommandInactiveTitlebar3", "Operations menu"), true));
    qobject->setCommandWindow1(
        mouseCommand(config.readEntry("CommandWindow1", "Activate, raise and pass click"), false));
    qobject->setCommandWindow2(
        mouseCommand(config.readEntry("CommandWindow2", "Activate and pass click"), false));
    qobject->setCommandWindow3(
        mouseCommand(config.readEntry("CommandWindow3", "Activate and pass click"), false));
    qobject->setCommandWindowWheel(
        mouseCommand(config.readEntry("CommandWindowWheel", "Scroll"), false));
    qobject->setCommandAll1(mouseCommand(config.readEntry("CommandAll1", "Move"), false));
    qobject->setCommandAll2(
        mouseCommand(config.readEntry("CommandAll2", "Toggle raise and lower"), false));
    qobject->setCommandAll3(mouseCommand(config.readEntry("CommandAll3", "Resize"), false));

    // TODO: should they be moved into reloadCompositingSettings?
    config = KConfigGroup(m_settings->config(), "Compositing");
    qobject->setMaxFpsInterval(1 * 1000 * 1000 * 1000
                               / config.readEntry("MaxFPS", options_qobject::defaultMaxFps()));
    qobject->setRefreshRate(config.readEntry("RefreshRate", options_qobject::defaultRefreshRate()));
    qobject->setVBlankTime(config.readEntry("VBlankTime", options_qobject::defaultVBlankTime())
                           * 1000); // config in micro, value in nano resolution

    // Modifier Only Shortcuts
    config = KConfigGroup(m_settings->config(), "ModifierOnlyShortcuts");
    m_modifierOnlyShortcuts.clear();
    if (config.hasKey("Shift")) {
        m_modifierOnlyShortcuts.insert(Qt::ShiftModifier, config.readEntry("Shift", QStringList()));
    }
    if (config.hasKey("Control")) {
        m_modifierOnlyShortcuts.insert(Qt::ControlModifier,
                                       config.readEntry("Control", QStringList()));
    }
    if (config.hasKey("Alt")) {
        m_modifierOnlyShortcuts.insert(Qt::AltModifier, config.readEntry("Alt", QStringList()));
    }
    m_modifierOnlyShortcuts.insert(
        Qt::MetaModifier,
        config.readEntry("Meta",
                         QStringList{QStringLiteral("org.kde.plasmashell"),
                                     QStringLiteral("/PlasmaShell"),
                                     QStringLiteral("org.kde.PlasmaShell"),
                                     QStringLiteral("activateLauncherMenu")}));
}

void options::syncFromKcfgc()
{
    qobject->setCondensedTitle(m_settings->condensedTitle());
    qobject->setFocusPolicy(m_settings->focusPolicy());
    qobject->setNextFocusPrefersMouse(m_settings->nextFocusPrefersMouse());
    qobject->setSeparateScreenFocus(m_settings->separateScreenFocus());
    current_output_follows_mouse = m_settings->activeMouseScreen();
    qobject->setRollOverDesktops(m_settings->rollOverDesktops());
    qobject->setFocusStealingPreventionLevel(
        static_cast<win::fsp_level>(m_settings->focusStealingPreventionLevel()));

#if KWIN_BUILD_DECORATIONS
    qobject->setPlacement(static_cast<win::placement>(m_settings->placement()));
#else
    qobject->setPlacement(win::placement::maximizing);
#endif

    qobject->setAutoRaise(m_settings->autoRaise());
    qobject->setAutoRaiseInterval(m_settings->autoRaiseInterval());
    qobject->setDelayFocusInterval(m_settings->delayFocusInterval());
    qobject->setClickRaise(m_settings->clickRaise());
    qobject->setBorderSnapZone(m_settings->borderSnapZone());
    qobject->setWindowSnapZone(m_settings->windowSnapZone());
    qobject->setCenterSnapZone(m_settings->centerSnapZone());
    qobject->setSnapOnlyWhenOverlapping(m_settings->snapOnlyWhenOverlapping());
    qobject->setKillPingTimeout(m_settings->killPingTimeout());
    qobject->setHideUtilityWindowsForInactive(m_settings->hideUtilityWindowsForInactive());
    qobject->setBorderlessMaximizedWindows(m_settings->borderlessMaximizedWindows());
    qobject->setElectricBorderMaximize(m_settings->electricBorderMaximize());
    qobject->setElectricBorderTiling(m_settings->electricBorderTiling());
    qobject->setElectricBorderCornerRatio(m_settings->electricBorderCornerRatio());
    qobject->setWindowsBlockCompositing(m_settings->windowsBlockCompositing());
    qobject->setAnimationCurve(m_settings->animationCurve());
}

bool options::loadCompositingConfig(bool force)
{
    KConfigGroup config(m_settings->config(), "Compositing");

    bool useCompositing = false;
    CompositingType compositingMode = NoCompositing;
    QString compositingBackend = config.readEntry("Backend", "OpenGL");
    if (compositingBackend == QStringLiteral("XRender"))
        compositingMode = XRenderCompositing;
    else if (compositingBackend == "QPainter")
        compositingMode = QPainterCompositing;
    else
        compositingMode = OpenGLCompositing;

    if (const char* c = getenv("KWIN_COMPOSE")) {
        switch (c[0]) {
        case 'O':
            qCDebug(KWIN_CORE) << "Compositing forced to OpenGL mode by environment variable";
            compositingMode = OpenGLCompositing;
            useCompositing = true;
            break;
        case 'X':
            qCDebug(KWIN_CORE) << "Compositing forced to XRender mode by environment variable";
            compositingMode = XRenderCompositing;
            useCompositing = true;
            break;
        case 'Q':
            qCDebug(KWIN_CORE) << "Compositing forced to QPainter mode by environment variable";
            compositingMode = QPainterCompositing;
            useCompositing = true;
            break;
        case 'N':
            if (getenv("KDE_FAILSAFE"))
                qCDebug(KWIN_CORE) << "Compositing disabled forcefully by KDE failsafe mode";
            else
                qCDebug(KWIN_CORE) << "Compositing disabled forcefully by environment variable";
            compositingMode = NoCompositing;
            break;
        default:
            qCDebug(KWIN_CORE) << "Unknown KWIN_COMPOSE mode set, ignoring";
            break;
        }
    }
    qobject->setCompositingMode(compositingMode);

    auto const platformSupportsNoCompositing = !kwinApp()->shouldUseWaylandForCompositing();
    if (qobject->m_compositingMode == NoCompositing && platformSupportsNoCompositing) {
        qobject->setUseCompositing(false);
        return false; // do not even detect compositing preferences if explicitly disabled
    }

    // it's either enforced by env or by initial resume from "suspend" or we check the settings
    qobject->setUseCompositing(useCompositing || force
                               || config.readEntry("Enabled",
                                                   options_qobject::defaultUseCompositing()
                                                       || !platformSupportsNoCompositing));

    if (!qobject->m_useCompositing)
        return false; // not enforced or necessary and not "enabled" by settings
    return true;
}

void options::reloadCompositingSettings(bool force)
{
    if (!loadCompositingConfig(force)) {
        return;
    }
    m_settings->load();
    syncFromKcfgc();

    // Compositing settings
    KConfigGroup config(m_settings->config(), "Compositing");

    qobject->setGlStrictBindingFollowsDriver(!config.hasKey("GLStrictBinding"));
    if (!qobject->isGlStrictBindingFollowsDriver()) {
        qobject->setGlStrictBinding(
            config.readEntry("GLStrictBinding", options_qobject::defaultGlStrictBinding()));
    }

    HiddenPreviews previews = options_qobject::defaultHiddenPreviews();
    // 4 - off, 5 - shown, 6 - always, other are old values
    int hps = config.readEntry("HiddenPreviews", 5);
    if (hps == 4)
        previews = HiddenPreviewsNever;
    else if (hps == 5)
        previews = HiddenPreviewsShown;
    else if (hps == 6)
        previews = HiddenPreviewsAlways;
    qobject->setHiddenPreviews(previews);
}

// restricted should be true for operations that the user may not be able to repeat
// if the window is moved out of the workspace (e.g. if the user moves a window
// by the titlebar, and moves it too high beneath Kicker at the top edge, they
// may not be able to move it back, unless they know about Meta+LMB)
options_qobject::WindowOperation options::windowOperation(const QString& name, bool restricted)
{
    if (name == QStringLiteral("Move"))
        return restricted ? options_qobject::MoveOp : options_qobject::UnrestrictedMoveOp;
    else if (name == QStringLiteral("Resize"))
        return restricted ? options_qobject::ResizeOp : options_qobject::UnrestrictedResizeOp;
    else if (name == QStringLiteral("Maximize"))
        return options_qobject::MaximizeOp;
    else if (name == QStringLiteral("Minimize"))
        return options_qobject::MinimizeOp;
    else if (name == QStringLiteral("Close"))
        return options_qobject::CloseOp;
    else if (name == QStringLiteral("OnAllDesktops"))
        return options_qobject::OnAllDesktopsOp;
    else if (name == QStringLiteral("Operations"))
        return options_qobject::OperationsOp;
    else if (name == QStringLiteral("Maximize (vertical only)"))
        return options_qobject::VMaximizeOp;
    else if (name == QStringLiteral("Maximize (horizontal only)"))
        return options_qobject::HMaximizeOp;
    else if (name == QStringLiteral("Lower"))
        return options_qobject::LowerOp;
    return options_qobject::NoOp;
}

options_qobject::MouseCommand options::mouseCommand(const QString& name, bool restricted)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise"))
        return options_qobject::MouseRaise;
    if (lowerName == QStringLiteral("lower"))
        return options_qobject::MouseLower;
    if (lowerName == QStringLiteral("operations menu"))
        return options_qobject::MouseOperationsMenu;
    if (lowerName == QStringLiteral("toggle raise and lower"))
        return options_qobject::MouseToggleRaiseAndLower;
    if (lowerName == QStringLiteral("activate and raise"))
        return options_qobject::MouseActivateAndRaise;
    if (lowerName == QStringLiteral("activate and lower"))
        return options_qobject::MouseActivateAndLower;
    if (lowerName == QStringLiteral("activate"))
        return options_qobject::MouseActivate;
    if (lowerName == QStringLiteral("activate, raise and pass click"))
        return options_qobject::MouseActivateRaiseAndPassClick;
    if (lowerName == QStringLiteral("activate and pass click"))
        return options_qobject::MouseActivateAndPassClick;
    if (lowerName == QStringLiteral("scroll"))
        return options_qobject::MouseNothing;
    if (lowerName == QStringLiteral("activate and scroll"))
        return options_qobject::MouseActivateAndPassClick;
    if (lowerName == QStringLiteral("activate, raise and scroll"))
        return options_qobject::MouseActivateRaiseAndPassClick;
    if (lowerName == QStringLiteral("activate, raise and move"))
        return restricted ? options_qobject::MouseActivateRaiseAndMove
                          : options_qobject::MouseActivateRaiseAndUnrestrictedMove;
    if (lowerName == QStringLiteral("move"))
        return restricted ? options_qobject::MouseMove : options_qobject::MouseUnrestrictedMove;
    if (lowerName == QStringLiteral("resize"))
        return restricted ? options_qobject::MouseResize : options_qobject::MouseUnrestrictedResize;
    if (lowerName == QStringLiteral("minimize"))
        return options_qobject::MouseMinimize;
    if (lowerName == QStringLiteral("close"))
        return options_qobject::MouseClose;
    if (lowerName == QStringLiteral("increase opacity"))
        return options_qobject::MouseOpacityMore;
    if (lowerName == QStringLiteral("decrease opacity"))
        return options_qobject::MouseOpacityLess;
    if (lowerName == QStringLiteral("nothing"))
        return options_qobject::MouseNothing;
    return options_qobject::MouseNothing;
}

options_qobject::MouseWheelCommand options::mouseWheelCommand(const QString& name)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise/lower"))
        return options_qobject::MouseWheelRaiseLower;
    if (lowerName == QStringLiteral("maximize/restore"))
        return options_qobject::MouseWheelMaximizeRestore;
    if (lowerName == QStringLiteral("above/below"))
        return options_qobject::MouseWheelAboveBelow;
    if (lowerName == QStringLiteral("previous/next desktop"))
        return options_qobject::MouseWheelPreviousNextDesktop;
    if (lowerName == QStringLiteral("change opacity"))
        return options_qobject::MouseWheelChangeOpacity;
    if (lowerName == QStringLiteral("nothing"))
        return options_qobject::MouseWheelNothing;
    return options_qobject::MouseWheelNothing;
}

bool options_qobject::condensedTitle() const
{
    return condensed_title;
}

options_qobject::MouseCommand options::wheelToMouseCommand(options_qobject::MouseWheelCommand com,
                                                           int delta) const
{
    switch (com) {
    case options_qobject::MouseWheelRaiseLower:
        return delta > 0 ? options_qobject::MouseRaise : options_qobject::MouseLower;
    case options_qobject::MouseWheelMaximizeRestore:
        return delta > 0 ? options_qobject::MouseMaximize : options_qobject::MouseRestore;
    case options_qobject::MouseWheelAboveBelow:
        return delta > 0 ? options_qobject::MouseAbove : options_qobject::MouseBelow;
    case options_qobject::MouseWheelPreviousNextDesktop:
        return delta > 0 ? options_qobject::MousePreviousDesktop
                         : options_qobject::MouseNextDesktop;
    case options_qobject::MouseWheelChangeOpacity:
        return delta > 0 ? options_qobject::MouseOpacityMore : options_qobject::MouseOpacityLess;
    default:
        return options_qobject::MouseNothing;
    }
}
#endif

double options::animationTimeFactor() const
{
#ifndef KCMRULES
    return m_settings->animationDurationFactor();
#else
    return 0;
#endif
}

options_qobject::WindowOperation
options_qobject::operationMaxButtonClick(Qt::MouseButtons button) const
{
    return button == Qt::RightButton ? opMaxButtonRightClick
        : button == Qt::MiddleButton ? opMaxButtonMiddleClick
                                     : opMaxButtonLeftClick;
}

QStringList options::modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const
{
    return m_modifierOnlyShortcuts.value(mod);
}

bool options_qobject::isUseCompositing() const
{
    return m_useCompositing || kwinApp()->shouldUseWaylandForCompositing();
}

options::options()
    : qobject{std::make_unique<options_qobject>()}
    , m_settings(new Settings(kwinApp()->config()))
{
    m_settings->setDefaults();
    syncFromKcfgc();

    m_configWatcher = KConfigWatcher::create(m_settings->sharedConfig());

    // TODO(romangg): Is this connect necessary? We don't do it for other config data.
    QObject::connect(m_configWatcher.data(),
                     &KConfigWatcher::configChanged,
                     qobject.get(),
                     [this](KConfigGroup const& group, QByteArrayList const& names) {
                         if (group.name() == QLatin1String("KDE")
                             && names.contains(QByteArrayLiteral("AnimationDurationFactor"))) {
                             Q_EMIT qobject->animationSpeedChanged();
                         }
                     });
}

options::~options() = default;

}
