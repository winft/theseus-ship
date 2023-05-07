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

#include "options_settings.h"
#include "utils/algorithm.h"

#include <kwingl/platform.h>

#include <QOpenGLContext>
#include <QProcess>

namespace KWin::base
{

OpenGLPlatformInterface defaultGlPlatformInterface(operation_mode mode)
{
    return should_use_wayland_for_compositing(mode) ? EglPlatformInterface : GlxPlatformInterface;
}

options_qobject::options_qobject(operation_mode mode)
    : windowing_mode{mode}
{
}

void options_qobject::setFocusPolicy(win::focus_policy focusPolicy)
{
    if (m_focusPolicy == focusPolicy) {
        return;
    }
    m_focusPolicy = focusPolicy;
    Q_EMIT focusPolicyChanged();
    if (m_focusPolicy == win::focus_policy::click) {
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
    if (m_focusPolicy == win::focus_policy::click) {
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
    if (m_focusPolicy == win::focus_policy::click) {
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
    if (m_focusPolicy == win::focus_policy::click) {
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

void options_qobject::setOperationTitlebarDblClick(win::win_op op)
{
    if (OpTitlebarDblClick == op) {
        return;
    }
    OpTitlebarDblClick = op;
    Q_EMIT operationTitlebarDblClickChanged();
}

void options_qobject::setOperationMaxButtonLeftClick(win::win_op op)
{
    if (opMaxButtonLeftClick == op) {
        return;
    }
    opMaxButtonLeftClick = op;
    Q_EMIT operationMaxButtonLeftClickChanged();
}

void options_qobject::setOperationMaxButtonRightClick(win::win_op op)
{
    if (opMaxButtonRightClick == op) {
        return;
    }
    opMaxButtonRightClick = op;
    Q_EMIT operationMaxButtonRightClickChanged();
}

void options_qobject::setOperationMaxButtonMiddleClick(win::win_op op)
{
    if (opMaxButtonMiddleClick == op) {
        return;
    }
    opMaxButtonMiddleClick = op;
    Q_EMIT operationMaxButtonMiddleClickChanged();
}

void options_qobject::setCommandActiveTitlebar1(win::mouse_cmd cmd)
{
    if (CmdActiveTitlebar1 == cmd) {
        return;
    }
    CmdActiveTitlebar1 = cmd;
    Q_EMIT commandActiveTitlebar1Changed();
}

void options_qobject::setCommandActiveTitlebar2(win::mouse_cmd cmd)
{
    if (CmdActiveTitlebar2 == cmd) {
        return;
    }
    CmdActiveTitlebar2 = cmd;
    Q_EMIT commandActiveTitlebar2Changed();
}

void options_qobject::setCommandActiveTitlebar3(win::mouse_cmd cmd)
{
    if (CmdActiveTitlebar3 == cmd) {
        return;
    }
    CmdActiveTitlebar3 = cmd;
    Q_EMIT commandActiveTitlebar3Changed();
}

void options_qobject::setCommandInactiveTitlebar1(win::mouse_cmd cmd)
{
    if (CmdInactiveTitlebar1 == cmd) {
        return;
    }
    CmdInactiveTitlebar1 = cmd;
    Q_EMIT commandInactiveTitlebar1Changed();
}

void options_qobject::setCommandInactiveTitlebar2(win::mouse_cmd cmd)
{
    if (CmdInactiveTitlebar2 == cmd) {
        return;
    }
    CmdInactiveTitlebar2 = cmd;
    Q_EMIT commandInactiveTitlebar2Changed();
}

void options_qobject::setCommandInactiveTitlebar3(win::mouse_cmd cmd)
{
    if (CmdInactiveTitlebar3 == cmd) {
        return;
    }
    CmdInactiveTitlebar3 = cmd;
    Q_EMIT commandInactiveTitlebar3Changed();
}

void options_qobject::setCommandWindow1(win::mouse_cmd cmd)
{
    if (CmdWindow1 == cmd) {
        return;
    }
    CmdWindow1 = cmd;
    Q_EMIT commandWindow1Changed();
}

void options_qobject::setCommandWindow2(win::mouse_cmd cmd)
{
    if (CmdWindow2 == cmd) {
        return;
    }
    CmdWindow2 = cmd;
    Q_EMIT commandWindow2Changed();
}

void options_qobject::setCommandWindow3(win::mouse_cmd cmd)
{
    if (CmdWindow3 == cmd) {
        return;
    }
    CmdWindow3 = cmd;
    Q_EMIT commandWindow3Changed();
}

void options_qobject::setCommandWindowWheel(win::mouse_cmd cmd)
{
    if (CmdWindowWheel == cmd) {
        return;
    }
    CmdWindowWheel = cmd;
    Q_EMIT commandWindowWheelChanged();
}

void options_qobject::setCommandAll1(win::mouse_cmd cmd)
{
    if (CmdAll1 == cmd) {
        return;
    }
    CmdAll1 = cmd;
    Q_EMIT commandAll1Changed();
}

void options_qobject::setCommandAll2(win::mouse_cmd cmd)
{
    if (CmdAll2 == cmd) {
        return;
    }
    CmdAll2 = cmd;
    Q_EMIT commandAll2Changed();
}

void options_qobject::setCommandAll3(win::mouse_cmd cmd)
{
    if (CmdAll3 == cmd) {
        return;
    }
    CmdAll3 = cmd;
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

void options_qobject::setHiddenPreviews(render::x11::hidden_preview hiddenPreviews)
{
    if (m_hiddenPreviews == hiddenPreviews) {
        return;
    }
    m_hiddenPreviews = hiddenPreviews;
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

void options_qobject::setAnimationCurve(render::animation_curve curve)
{
    if (m_animationCurve == curve) {
        return;
    }

    qCDebug(KWIN_CORE) << "Setting animation curve: " << static_cast<int>(curve);
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
    if (compositingBackend == "QPainter")
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

    auto const platformSupportsNoCompositing
        = !should_use_wayland_for_compositing(qobject->windowing_mode);

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

    auto previews = options_qobject::defaultHiddenPreviews();
    // 4 - off, 5 - shown, 6 - always, other are old values
    int hps = config.readEntry("HiddenPreviews", 5);
    if (hps == 4)
        previews = render::x11::hidden_preview::never;
    else if (hps == 5)
        previews = render::x11::hidden_preview::shown;
    else if (hps == 6)
        previews = render::x11::hidden_preview::always;
    qobject->setHiddenPreviews(previews);
}

// restricted should be true for operations that the user may not be able to repeat
// if the window is moved out of the workspace (e.g. if the user moves a window
// by the titlebar, and moves it too high beneath Kicker at the top edge, they
// may not be able to move it back, unless they know about Meta+LMB)
win::win_op options::windowOperation(const QString& name, bool restricted)
{
    if (name == QStringLiteral("Move"))
        return restricted ? win::win_op::move : win::win_op::unrestricted_move;
    else if (name == QStringLiteral("Resize"))
        return restricted ? win::win_op::resize : win::win_op::unrestricted_resize;
    else if (name == QStringLiteral("Maximize"))
        return win::win_op::maximize;
    else if (name == QStringLiteral("Minimize"))
        return win::win_op::minimize;
    else if (name == QStringLiteral("Close"))
        return win::win_op::close;
    else if (name == QStringLiteral("OnAllDesktops"))
        return win::win_op::on_all_desktops;
    else if (name == QStringLiteral("Operations"))
        return win::win_op::operations;
    else if (name == QStringLiteral("Maximize (vertical only)"))
        return win::win_op::v_maximize;
    else if (name == QStringLiteral("Maximize (horizontal only)"))
        return win::win_op::h_maximize;
    else if (name == QStringLiteral("Lower"))
        return win::win_op::lower;
    return win::win_op::noop;
}

win::mouse_cmd options::mouseCommand(const QString& name, bool restricted)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise")) {
        return win::mouse_cmd::raise;
    }
    if (lowerName == QStringLiteral("lower")) {
        return win::mouse_cmd::lower;
    }
    if (lowerName == QStringLiteral("operations menu")) {
        return win::mouse_cmd::operations_menu;
    }
    if (lowerName == QStringLiteral("toggle raise and lower")) {
        return win::mouse_cmd::toggle_raise_and_lower;
    }
    if (lowerName == QStringLiteral("activate and raise")) {
        return win::mouse_cmd::activate_and_raise;
    }
    if (lowerName == QStringLiteral("activate and lower")) {
        return win::mouse_cmd::activate_and_lower;
    }
    if (lowerName == QStringLiteral("activate")) {
        return win::mouse_cmd::activate;
    }
    if (lowerName == QStringLiteral("activate, raise and pass click")) {
        return win::mouse_cmd::activate_raise_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate and pass click")) {
        return win::mouse_cmd::activate_and_pass_click;
    }
    if (lowerName == QStringLiteral("scroll")) {
        return win::mouse_cmd::nothing;
    }
    if (lowerName == QStringLiteral("activate and scroll")) {
        return win::mouse_cmd::activate_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate, raise and scroll")) {
        return win::mouse_cmd::activate_raise_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate, raise and move")) {
        return restricted ? win::mouse_cmd::activate_raise_and_move
                          : win::mouse_cmd::activate_raise_and_unrestricted_move;
    }
    if (lowerName == QStringLiteral("move")) {
        return restricted ? win::mouse_cmd::move : win::mouse_cmd::unrestricted_move;
    }
    if (lowerName == QStringLiteral("resize")) {
        return restricted ? win::mouse_cmd::resize : win::mouse_cmd::unrestricted_resize;
    }
    if (lowerName == QStringLiteral("minimize")) {
        return win::mouse_cmd::minimize;
    }
    if (lowerName == QStringLiteral("close")) {
        return win::mouse_cmd::close;
    }
    if (lowerName == QStringLiteral("increase opacity")) {
        return win::mouse_cmd::opacity_more;
    }
    if (lowerName == QStringLiteral("decrease opacity")) {
        return win::mouse_cmd::opacity_less;
    }
    if (lowerName == QStringLiteral("nothing")) {
        return win::mouse_cmd::nothing;
    }
    return win::mouse_cmd::nothing;
}

win::mouse_wheel_cmd options::mouseWheelCommand(const QString& name)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise/lower"))
        return win::mouse_wheel_cmd::raise_lower;
    if (lowerName == QStringLiteral("maximize/restore"))
        return win::mouse_wheel_cmd::maximize_restore;
    if (lowerName == QStringLiteral("above/below"))
        return win::mouse_wheel_cmd::above_below;
    if (lowerName == QStringLiteral("previous/next desktop"))
        return win::mouse_wheel_cmd::previous_next_desktop;
    if (lowerName == QStringLiteral("change opacity"))
        return win::mouse_wheel_cmd::change_opacity;
    if (lowerName == QStringLiteral("nothing"))
        return win::mouse_wheel_cmd::nothing;
    return win::mouse_wheel_cmd::nothing;
}

bool options_qobject::condensedTitle() const
{
    return condensed_title;
}

win::mouse_cmd options::wheelToMouseCommand(win::mouse_wheel_cmd com, int delta) const
{
    switch (com) {
    case win::mouse_wheel_cmd::raise_lower:
        return delta > 0 ? win::mouse_cmd::raise : win::mouse_cmd::lower;
    case win::mouse_wheel_cmd::maximize_restore:
        return delta > 0 ? win::mouse_cmd::maximize : win::mouse_cmd::restore;
    case win::mouse_wheel_cmd::above_below:
        return delta > 0 ? win::mouse_cmd::above : win::mouse_cmd::below;
    case win::mouse_wheel_cmd::previous_next_desktop:
        return delta > 0 ? win::mouse_cmd::previous_desktop : win::mouse_cmd::next_desktop;
    case win::mouse_wheel_cmd::change_opacity:
        return delta > 0 ? win::mouse_cmd::opacity_more : win::mouse_cmd::opacity_less;
    default:
        return win::mouse_cmd::nothing;
    }
}

double options::animationTimeFactor() const
{
#ifndef KCMRULES
    return m_settings->animationDurationFactor();
#else
    return 0;
#endif
}

win::win_op options_qobject::operationMaxButtonClick(Qt::MouseButtons button) const
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
    return m_useCompositing || should_use_wayland_for_compositing(windowing_mode);
}

options::options(operation_mode mode, KSharedConfigPtr config)
    : qobject{std::make_unique<options_qobject>(mode)}
    , m_settings(new Settings(config))
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
