/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "base/logging.h"
#include "base/platform.h"
#include "config-kwin.h"
#include "render/platform.h"

#ifndef KCMRULES

#include <QProcess>

#include "screens.h"
#include "settings.h"
#include <QOpenGLContext>
#include <kwinglplatform.h>

#endif // KCMRULES

namespace KWin::base
{

#ifndef KCMRULES

int currentRefreshRate()
{
    return options::currentRefreshRate();
}

int options::currentRefreshRate()
{
    int rate = -1;
    QString syncScreenName(QLatin1String("primary screen"));
    if (kwinApp()->options->refreshRate() > 0) { // use manually configured refresh rate
        rate = kwinApp()->options->refreshRate();
    } else if (kwinApp()->get_base().screens.count() > 0) {
        // prefer the refreshrate calculated from the screens mode information
        // at least the nvidia driver reports 50Hz BS ... *again*!
        auto const& screens = kwinApp()->get_base().screens;
        int syncScreen = 0;
        if (screens.count() > 1) {
            const QByteArray syncDisplayDevice(qgetenv("__GL_SYNC_DISPLAY_DEVICE"));
            // if __GL_SYNC_DISPLAY_DEVICE is exported, the GPU shall sync to that device
            // so we try to use its refresh rate
            if (!syncDisplayDevice.isEmpty()) {
                for (int i = 0; i < screens.count(); ++i) {
                    if (screens.name(i) == syncDisplayDevice) {
                        syncScreenName = screens.name(i);
                        syncScreen = i;
                        break;
                    }
                }
            }
        }
        // TODO forward float precision?
        rate = qRound(screens.refreshRate(syncScreen));
    }

    // 0Hz or less is invalid, so we fallback to a default rate
    if (rate <= 0)
        rate = 60; // and not shitty 50Hz for sure! *grrr*

    // QTimer gives us 1msec (1000Hz) at best, so we ignore anything higher;
    // however, additional throttling prevents very high rates from taking place anyway
    else if (rate > 1000)
        rate = 1000;
    qCDebug(KWIN_CORE) << "Vertical Refresh rate " << rate << "Hz (" << syncScreenName << ")";
    return rate;
}

options::options()
    : m_settings(new Settings(kwinApp()->config()))
    , m_focusPolicy(ClickToFocus)
    , m_nextFocusPrefersMouse(false)
    , m_clickRaise(false)
    , m_autoRaise(false)
    , m_autoRaiseInterval(0)
    , m_delayFocusInterval(0)
    , m_separateScreenFocus(false)
    , m_placement(win::placement::no_placement)
    , m_borderSnapZone(0)
    , m_windowSnapZone(0)
    , m_centerSnapZone(0)
    , m_snapOnlyWhenOverlapping(false)
    , m_rollOverDesktops(false)
    , m_focusStealingPreventionLevel(0)
    , m_killPingTimeout(0)
    , m_hideUtilityWindowsForInactive(false)
    , m_compositingMode(options::defaultCompositingMode())
    , m_useCompositing(options::defaultUseCompositing())
    , m_hiddenPreviews(options::defaultHiddenPreviews())
    , m_maxFpsInterval(options::defaultMaxFpsInterval())
    , m_refreshRate(options::defaultRefreshRate())
    , m_vBlankTime(options::defaultVBlankTime())
    , m_glStrictBinding(options::defaultGlStrictBinding())
    , m_glStrictBindingFollowsDriver(options::defaultGlStrictBindingFollowsDriver())
    , m_glPlatformInterface(options::defaultGlPlatformInterface())
    , m_windowsBlockCompositing(true)
    , m_animationCurve(AnimationCurve::Linear)
    , OpTitlebarDblClick(options::defaultOperationTitlebarDblClick())
    , CmdActiveTitlebar1(options::defaultCommandActiveTitlebar1())
    , CmdActiveTitlebar2(options::defaultCommandActiveTitlebar2())
    , CmdActiveTitlebar3(options::defaultCommandActiveTitlebar3())
    , CmdInactiveTitlebar1(options::defaultCommandInactiveTitlebar1())
    , CmdInactiveTitlebar2(options::defaultCommandInactiveTitlebar2())
    , CmdInactiveTitlebar3(options::defaultCommandInactiveTitlebar3())
    , CmdTitlebarWheel(options::defaultCommandTitlebarWheel())
    , CmdWindow1(options::defaultCommandWindow1())
    , CmdWindow2(options::defaultCommandWindow2())
    , CmdWindow3(options::defaultCommandWindow3())
    , CmdWindowWheel(options::defaultCommandWindowWheel())
    , CmdAll1(options::defaultCommandAll1())
    , CmdAll2(options::defaultCommandAll2())
    , CmdAll3(options::defaultCommandAll3())
    , CmdAllWheel(options::defaultCommandAllWheel())
    , CmdAllModKey(options::defaultKeyCmdAllModKey())
    , electric_border_maximize(false)
    , electric_border_tiling(false)
    , electric_border_corner_ratio(0.0)
    , borderless_maximized_windows(false)
    , condensed_title(false)
{
    m_settings->setDefaults();
    syncFromKcfgc();

    m_configWatcher = KConfigWatcher::create(m_settings->sharedConfig());
    connect(m_configWatcher.data(),
            &KConfigWatcher::configChanged,
            this,
            [this](const KConfigGroup& group, const QByteArrayList& names) {
                if (group.name() == QLatin1String("KDE")
                    && names.contains(QByteArrayLiteral("AnimationDurationFactor"))) {
                    Q_EMIT animationSpeedChanged();
                }
            });
}

options::~options()
{
}

void options::setFocusPolicy(FocusPolicy focusPolicy)
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

void options::setNextFocusPrefersMouse(bool nextFocusPrefersMouse)
{
    if (m_nextFocusPrefersMouse == nextFocusPrefersMouse) {
        return;
    }
    m_nextFocusPrefersMouse = nextFocusPrefersMouse;
    Q_EMIT nextFocusPrefersMouseChanged();
}

void options::setClickRaise(bool clickRaise)
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

void options::setAutoRaise(bool autoRaise)
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

void options::setAutoRaiseInterval(int autoRaiseInterval)
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

void options::setDelayFocusInterval(int delayFocusInterval)
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

void options::setSeparateScreenFocus(bool separateScreenFocus)
{
    if (m_separateScreenFocus == separateScreenFocus) {
        return;
    }
    m_separateScreenFocus = separateScreenFocus;
    Q_EMIT separateScreenFocusChanged(m_separateScreenFocus);
}

void options::setPlacement(win::placement placement)
{
    if (m_placement == placement) {
        return;
    }
    m_placement = placement;
    Q_EMIT placementChanged();
}

void options::setBorderSnapZone(int borderSnapZone)
{
    if (m_borderSnapZone == borderSnapZone) {
        return;
    }
    m_borderSnapZone = borderSnapZone;
    Q_EMIT borderSnapZoneChanged();
}

void options::setWindowSnapZone(int windowSnapZone)
{
    if (m_windowSnapZone == windowSnapZone) {
        return;
    }
    m_windowSnapZone = windowSnapZone;
    Q_EMIT windowSnapZoneChanged();
}

void options::setCenterSnapZone(int centerSnapZone)
{
    if (m_centerSnapZone == centerSnapZone) {
        return;
    }
    m_centerSnapZone = centerSnapZone;
    Q_EMIT centerSnapZoneChanged();
}

void options::setSnapOnlyWhenOverlapping(bool snapOnlyWhenOverlapping)
{
    if (m_snapOnlyWhenOverlapping == snapOnlyWhenOverlapping) {
        return;
    }
    m_snapOnlyWhenOverlapping = snapOnlyWhenOverlapping;
    Q_EMIT snapOnlyWhenOverlappingChanged();
}

void options::setRollOverDesktops(bool rollOverDesktops)
{
    if (m_rollOverDesktops == rollOverDesktops) {
        return;
    }
    m_rollOverDesktops = rollOverDesktops;
    Q_EMIT rollOverDesktopsChanged(m_rollOverDesktops);
}

void options::setFocusStealingPreventionLevel(int focusStealingPreventionLevel)
{
    if (!focusPolicyIsReasonable()) {
        focusStealingPreventionLevel = 0;
    }
    if (m_focusStealingPreventionLevel == focusStealingPreventionLevel) {
        return;
    }
    m_focusStealingPreventionLevel = qMax(0, qMin(4, focusStealingPreventionLevel));
    Q_EMIT focusStealingPreventionLevelChanged();
}

void options::setOperationTitlebarDblClick(WindowOperation operationTitlebarDblClick)
{
    if (OpTitlebarDblClick == operationTitlebarDblClick) {
        return;
    }
    OpTitlebarDblClick = operationTitlebarDblClick;
    Q_EMIT operationTitlebarDblClickChanged();
}

void options::setOperationMaxButtonLeftClick(WindowOperation op)
{
    if (opMaxButtonLeftClick == op) {
        return;
    }
    opMaxButtonLeftClick = op;
    Q_EMIT operationMaxButtonLeftClickChanged();
}

void options::setOperationMaxButtonRightClick(WindowOperation op)
{
    if (opMaxButtonRightClick == op) {
        return;
    }
    opMaxButtonRightClick = op;
    Q_EMIT operationMaxButtonRightClickChanged();
}

void options::setOperationMaxButtonMiddleClick(WindowOperation op)
{
    if (opMaxButtonMiddleClick == op) {
        return;
    }
    opMaxButtonMiddleClick = op;
    Q_EMIT operationMaxButtonMiddleClickChanged();
}

void options::setCommandActiveTitlebar1(MouseCommand commandActiveTitlebar1)
{
    if (CmdActiveTitlebar1 == commandActiveTitlebar1) {
        return;
    }
    CmdActiveTitlebar1 = commandActiveTitlebar1;
    Q_EMIT commandActiveTitlebar1Changed();
}

void options::setCommandActiveTitlebar2(MouseCommand commandActiveTitlebar2)
{
    if (CmdActiveTitlebar2 == commandActiveTitlebar2) {
        return;
    }
    CmdActiveTitlebar2 = commandActiveTitlebar2;
    Q_EMIT commandActiveTitlebar2Changed();
}

void options::setCommandActiveTitlebar3(MouseCommand commandActiveTitlebar3)
{
    if (CmdActiveTitlebar3 == commandActiveTitlebar3) {
        return;
    }
    CmdActiveTitlebar3 = commandActiveTitlebar3;
    Q_EMIT commandActiveTitlebar3Changed();
}

void options::setCommandInactiveTitlebar1(MouseCommand commandInactiveTitlebar1)
{
    if (CmdInactiveTitlebar1 == commandInactiveTitlebar1) {
        return;
    }
    CmdInactiveTitlebar1 = commandInactiveTitlebar1;
    Q_EMIT commandInactiveTitlebar1Changed();
}

void options::setCommandInactiveTitlebar2(MouseCommand commandInactiveTitlebar2)
{
    if (CmdInactiveTitlebar2 == commandInactiveTitlebar2) {
        return;
    }
    CmdInactiveTitlebar2 = commandInactiveTitlebar2;
    Q_EMIT commandInactiveTitlebar2Changed();
}

void options::setCommandInactiveTitlebar3(MouseCommand commandInactiveTitlebar3)
{
    if (CmdInactiveTitlebar3 == commandInactiveTitlebar3) {
        return;
    }
    CmdInactiveTitlebar3 = commandInactiveTitlebar3;
    Q_EMIT commandInactiveTitlebar3Changed();
}

void options::setCommandWindow1(MouseCommand commandWindow1)
{
    if (CmdWindow1 == commandWindow1) {
        return;
    }
    CmdWindow1 = commandWindow1;
    Q_EMIT commandWindow1Changed();
}

void options::setCommandWindow2(MouseCommand commandWindow2)
{
    if (CmdWindow2 == commandWindow2) {
        return;
    }
    CmdWindow2 = commandWindow2;
    Q_EMIT commandWindow2Changed();
}

void options::setCommandWindow3(MouseCommand commandWindow3)
{
    if (CmdWindow3 == commandWindow3) {
        return;
    }
    CmdWindow3 = commandWindow3;
    Q_EMIT commandWindow3Changed();
}

void options::setCommandWindowWheel(MouseCommand commandWindowWheel)
{
    if (CmdWindowWheel == commandWindowWheel) {
        return;
    }
    CmdWindowWheel = commandWindowWheel;
    Q_EMIT commandWindowWheelChanged();
}

void options::setCommandAll1(MouseCommand commandAll1)
{
    if (CmdAll1 == commandAll1) {
        return;
    }
    CmdAll1 = commandAll1;
    Q_EMIT commandAll1Changed();
}

void options::setCommandAll2(MouseCommand commandAll2)
{
    if (CmdAll2 == commandAll2) {
        return;
    }
    CmdAll2 = commandAll2;
    Q_EMIT commandAll2Changed();
}

void options::setCommandAll3(MouseCommand commandAll3)
{
    if (CmdAll3 == commandAll3) {
        return;
    }
    CmdAll3 = commandAll3;
    Q_EMIT commandAll3Changed();
}

void options::setKeyCmdAllModKey(uint keyCmdAllModKey)
{
    if (CmdAllModKey == keyCmdAllModKey) {
        return;
    }
    CmdAllModKey = keyCmdAllModKey;
    Q_EMIT keyCmdAllModKeyChanged();
}

void options::setCondensedTitle(bool condensedTitle)
{
    if (condensed_title == condensedTitle) {
        return;
    }
    condensed_title = condensedTitle;
    Q_EMIT condensedTitleChanged();
}

void options::setElectricBorderMaximize(bool electricBorderMaximize)
{
    if (electric_border_maximize == electricBorderMaximize) {
        return;
    }
    electric_border_maximize = electricBorderMaximize;
    Q_EMIT electricBorderMaximizeChanged();
}

void options::setElectricBorderTiling(bool electricBorderTiling)
{
    if (electric_border_tiling == electricBorderTiling) {
        return;
    }
    electric_border_tiling = electricBorderTiling;
    Q_EMIT electricBorderTilingChanged();
}

void options::setElectricBorderCornerRatio(float electricBorderCornerRatio)
{
    if (electric_border_corner_ratio == electricBorderCornerRatio) {
        return;
    }
    electric_border_corner_ratio = electricBorderCornerRatio;
    Q_EMIT electricBorderCornerRatioChanged();
}

void options::setBorderlessMaximizedWindows(bool borderlessMaximizedWindows)
{
    if (borderless_maximized_windows == borderlessMaximizedWindows) {
        return;
    }
    borderless_maximized_windows = borderlessMaximizedWindows;
    Q_EMIT borderlessMaximizedWindowsChanged();
}

void options::setKillPingTimeout(int killPingTimeout)
{
    if (m_killPingTimeout == killPingTimeout) {
        return;
    }
    m_killPingTimeout = killPingTimeout;
    Q_EMIT killPingTimeoutChanged();
}

void options::setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive)
{
    if (m_hideUtilityWindowsForInactive == hideUtilityWindowsForInactive) {
        return;
    }
    m_hideUtilityWindowsForInactive = hideUtilityWindowsForInactive;
    Q_EMIT hideUtilityWindowsForInactiveChanged();
}

void options::setCompositingMode(int compositingMode)
{
    if (m_compositingMode == static_cast<CompositingType>(compositingMode)) {
        return;
    }
    m_compositingMode = static_cast<CompositingType>(compositingMode);
    Q_EMIT compositingModeChanged();
}

void options::setUseCompositing(bool useCompositing)
{
    if (m_useCompositing == useCompositing) {
        return;
    }
    m_useCompositing = useCompositing;
    Q_EMIT useCompositingChanged();
}

void options::setHiddenPreviews(int hiddenPreviews)
{
    if (m_hiddenPreviews == static_cast<HiddenPreviews>(hiddenPreviews)) {
        return;
    }
    m_hiddenPreviews = static_cast<HiddenPreviews>(hiddenPreviews);
    Q_EMIT hiddenPreviewsChanged();
}

void options::setMaxFpsInterval(qint64 maxFpsInterval)
{
    if (m_maxFpsInterval == maxFpsInterval) {
        return;
    }
    m_maxFpsInterval = maxFpsInterval;
    Q_EMIT maxFpsIntervalChanged();
}

void options::setRefreshRate(uint refreshRate)
{
    if (m_refreshRate == refreshRate) {
        return;
    }
    m_refreshRate = refreshRate;
    Q_EMIT refreshRateChanged();
}

void options::setVBlankTime(qint64 vBlankTime)
{
    if (m_vBlankTime == vBlankTime) {
        return;
    }
    m_vBlankTime = vBlankTime;
    Q_EMIT vBlankTimeChanged();
}

void options::setGlStrictBinding(bool glStrictBinding)
{
    if (m_glStrictBinding == glStrictBinding) {
        return;
    }
    m_glStrictBinding = glStrictBinding;
    Q_EMIT glStrictBindingChanged();
}

void options::setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver)
{
    if (m_glStrictBindingFollowsDriver == glStrictBindingFollowsDriver) {
        return;
    }
    m_glStrictBindingFollowsDriver = glStrictBindingFollowsDriver;
    Q_EMIT glStrictBindingFollowsDriverChanged();
}

void options::setWindowsBlockCompositing(bool value)
{
    if (m_windowsBlockCompositing == value) {
        return;
    }
    m_windowsBlockCompositing = value;
    Q_EMIT windowsBlockCompositingChanged();
}

void options::setAnimationCurve(AnimationCurve curve)
{
    if (m_animationCurve == curve) {
        return;
    }

    qCDebug(KWIN_CORE) << "Setting animation curve: " << curve;
    m_animationCurve = curve;
    Q_EMIT animationCurveChanged();
}

void options::setGlPlatformInterface(OpenGLPlatformInterface interface)
{
    // check environment variable
    const QByteArray envOpenGLInterface(qgetenv("KWIN_OPENGL_INTERFACE"));
    if (!envOpenGLInterface.isEmpty()) {
        if (qstrcmp(envOpenGLInterface, "egl") == 0) {
            qCDebug(KWIN_CORE) << "Forcing EGL native interface through environment variable";
            interface = EglPlatformInterface;
        } else if (qstrcmp(envOpenGLInterface, "glx") == 0) {
            qCDebug(KWIN_CORE) << "Forcing GLX native interface through environment variable";
            interface = GlxPlatformInterface;
        }
    }
    if (kwinApp()->shouldUseWaylandForCompositing() && interface == GlxPlatformInterface) {
        // Glx is impossible on Wayland, enforce egl
        qCDebug(KWIN_CORE) << "Forcing EGL native interface for Wayland mode";
        interface = EglPlatformInterface;
    }
#if !HAVE_EPOXY_GLX
    qCDebug(KWIN_CORE) << "Forcing EGL native interface as compiled without GLX support";
    interface = EglPlatformInterface;
#endif
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        qCDebug(KWIN_CORE) << "Forcing EGL native interface as Qt uses OpenGL ES";
        interface = EglPlatformInterface;
    } else if (qstrcmp(qgetenv("KWIN_COMPOSE"), "O2ES") == 0) {
        qCDebug(KWIN_CORE) << "Forcing EGL native interface as OpenGL ES requested through "
                              "KWIN_COMPOSE environment variable.";
        interface = EglPlatformInterface;
    }

    if (m_glPlatformInterface == interface) {
        return;
    }
    m_glPlatformInterface = interface;
    Q_EMIT glPlatformInterfaceChanged();
}

void options::reparseConfiguration()
{
    m_settings->config()->reparseConfiguration();
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

    Q_EMIT configChanged();
}

void options::loadConfig()
{
    m_settings->load();

    syncFromKcfgc();

    // Electric borders
    KConfigGroup config(m_settings->config(), "Windows");
    OpTitlebarDblClick
        = windowOperation(config.readEntry("TitlebarDoubleClickCommand", "Maximize"), true);
    setOperationMaxButtonLeftClick(
        windowOperation(config.readEntry("MaximizeButtonLeftClickCommand", "Maximize"), true));
    setOperationMaxButtonMiddleClick(windowOperation(
        config.readEntry("MaximizeButtonMiddleClickCommand", "Maximize (vertical only)"), true));
    setOperationMaxButtonRightClick(windowOperation(
        config.readEntry("MaximizeButtonRightClickCommand", "Maximize (horizontal only)"), true));

    // Mouse bindings
    config = KConfigGroup(m_settings->config(), "MouseBindings");
    // TODO: add properties for missing options
    CmdTitlebarWheel = mouseWheelCommand(config.readEntry("CommandTitlebarWheel", "Nothing"));
    CmdAllModKey = (config.readEntry("CommandAllKey", "Meta") == QStringLiteral("Meta"))
        ? Qt::Key_Meta
        : Qt::Key_Alt;
    CmdAllWheel = mouseWheelCommand(config.readEntry("CommandAllWheel", "Nothing"));
    setCommandActiveTitlebar1(
        mouseCommand(config.readEntry("CommandActiveTitlebar1", "Raise"), true));
    setCommandActiveTitlebar2(
        mouseCommand(config.readEntry("CommandActiveTitlebar2", "Nothing"), true));
    setCommandActiveTitlebar3(
        mouseCommand(config.readEntry("CommandActiveTitlebar3", "Operations menu"), true));
    setCommandInactiveTitlebar1(
        mouseCommand(config.readEntry("CommandInactiveTitlebar1", "Activate and raise"), true));
    setCommandInactiveTitlebar2(
        mouseCommand(config.readEntry("CommandInactiveTitlebar2", "Nothing"), true));
    setCommandInactiveTitlebar3(
        mouseCommand(config.readEntry("CommandInactiveTitlebar3", "Operations menu"), true));
    setCommandWindow1(
        mouseCommand(config.readEntry("CommandWindow1", "Activate, raise and pass click"), false));
    setCommandWindow2(
        mouseCommand(config.readEntry("CommandWindow2", "Activate and pass click"), false));
    setCommandWindow3(
        mouseCommand(config.readEntry("CommandWindow3", "Activate and pass click"), false));
    setCommandWindowWheel(mouseCommand(config.readEntry("CommandWindowWheel", "Scroll"), false));
    setCommandAll1(mouseCommand(config.readEntry("CommandAll1", "Move"), false));
    setCommandAll2(mouseCommand(config.readEntry("CommandAll2", "Toggle raise and lower"), false));
    setCommandAll3(mouseCommand(config.readEntry("CommandAll3", "Resize"), false));

    // TODO: should they be moved into reloadCompositingSettings?
    config = KConfigGroup(m_settings->config(), "Compositing");
    setMaxFpsInterval(1 * 1000 * 1000 * 1000
                      / config.readEntry("MaxFPS", options::defaultMaxFps()));
    setRefreshRate(config.readEntry("RefreshRate", options::defaultRefreshRate()));
    setVBlankTime(config.readEntry("VBlankTime", options::defaultVBlankTime())
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
    setCondensedTitle(m_settings->condensedTitle());
    setFocusPolicy(m_settings->focusPolicy());
    setNextFocusPrefersMouse(m_settings->nextFocusPrefersMouse());
    setSeparateScreenFocus(m_settings->separateScreenFocus());
    setRollOverDesktops(m_settings->rollOverDesktops());
    setFocusStealingPreventionLevel(m_settings->focusStealingPreventionLevel());

#ifdef KWIN_BUILD_DECORATIONS
    setPlacement(static_cast<win::placement>(m_settings->placement()));
#else
    setPlacement(win::placement::maximizing);
#endif

    setAutoRaise(m_settings->autoRaise());
    setAutoRaiseInterval(m_settings->autoRaiseInterval());
    setDelayFocusInterval(m_settings->delayFocusInterval());
    setClickRaise(m_settings->clickRaise());
    setBorderSnapZone(m_settings->borderSnapZone());
    setWindowSnapZone(m_settings->windowSnapZone());
    setCenterSnapZone(m_settings->centerSnapZone());
    setSnapOnlyWhenOverlapping(m_settings->snapOnlyWhenOverlapping());
    setKillPingTimeout(m_settings->killPingTimeout());
    setHideUtilityWindowsForInactive(m_settings->hideUtilityWindowsForInactive());
    setBorderlessMaximizedWindows(m_settings->borderlessMaximizedWindows());
    setElectricBorderMaximize(m_settings->electricBorderMaximize());
    setElectricBorderTiling(m_settings->electricBorderTiling());
    setElectricBorderCornerRatio(m_settings->electricBorderCornerRatio());
    setWindowsBlockCompositing(m_settings->windowsBlockCompositing());
    setAnimationCurve(m_settings->animationCurve());
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
    setCompositingMode(compositingMode);

    const bool platformSupportsNoCompositing
        = kwinApp()->get_base().render->supportedCompositors().contains(NoCompositing);
    if (m_compositingMode == NoCompositing && platformSupportsNoCompositing) {
        setUseCompositing(false);
        return false; // do not even detect compositing preferences if explicitly disabled
    }

    // it's either enforced by env or by initial resume from "suspend" or we check the settings
    setUseCompositing(
        useCompositing || force
        || config.readEntry("Enabled",
                            options::defaultUseCompositing() || !platformSupportsNoCompositing));

    if (!m_useCompositing)
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

    setGlStrictBindingFollowsDriver(!config.hasKey("GLStrictBinding"));
    if (!isGlStrictBindingFollowsDriver()) {
        setGlStrictBinding(config.readEntry("GLStrictBinding", options::defaultGlStrictBinding()));
    }

    HiddenPreviews previews = options::defaultHiddenPreviews();
    // 4 - off, 5 - shown, 6 - always, other are old values
    int hps = config.readEntry("HiddenPreviews", 5);
    if (hps == 4)
        previews = HiddenPreviewsNever;
    else if (hps == 5)
        previews = HiddenPreviewsShown;
    else if (hps == 6)
        previews = HiddenPreviewsAlways;
    setHiddenPreviews(previews);

    auto interfaceToKey = [](OpenGLPlatformInterface interface) {
        switch (interface) {
        case GlxPlatformInterface:
            return QStringLiteral("glx");
        case EglPlatformInterface:
            return QStringLiteral("egl");
        default:
            return QString();
        }
    };
    auto keyToInterface = [](const QString& key) {
        if (key == QStringLiteral("glx")) {
            return GlxPlatformInterface;
        } else if (key == QStringLiteral("egl")) {
            return EglPlatformInterface;
        }
        return defaultGlPlatformInterface();
    };
    setGlPlatformInterface(keyToInterface(
        config.readEntry("GLPlatformInterface", interfaceToKey(m_glPlatformInterface))));
}

// restricted should be true for operations that the user may not be able to repeat
// if the window is moved out of the workspace (e.g. if the user moves a window
// by the titlebar, and moves it too high beneath Kicker at the top edge, they
// may not be able to move it back, unless they know about Meta+LMB)
options::WindowOperation options::windowOperation(const QString& name, bool restricted)
{
    if (name == QStringLiteral("Move"))
        return restricted ? MoveOp : UnrestrictedMoveOp;
    else if (name == QStringLiteral("Resize"))
        return restricted ? ResizeOp : UnrestrictedResizeOp;
    else if (name == QStringLiteral("Maximize"))
        return MaximizeOp;
    else if (name == QStringLiteral("Minimize"))
        return MinimizeOp;
    else if (name == QStringLiteral("Close"))
        return CloseOp;
    else if (name == QStringLiteral("OnAllDesktops"))
        return OnAllDesktopsOp;
    else if (name == QStringLiteral("Operations"))
        return OperationsOp;
    else if (name == QStringLiteral("Maximize (vertical only)"))
        return VMaximizeOp;
    else if (name == QStringLiteral("Maximize (horizontal only)"))
        return HMaximizeOp;
    else if (name == QStringLiteral("Lower"))
        return LowerOp;
    return NoOp;
}

options::MouseCommand options::mouseCommand(const QString& name, bool restricted)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise"))
        return MouseRaise;
    if (lowerName == QStringLiteral("lower"))
        return MouseLower;
    if (lowerName == QStringLiteral("operations menu"))
        return MouseOperationsMenu;
    if (lowerName == QStringLiteral("toggle raise and lower"))
        return MouseToggleRaiseAndLower;
    if (lowerName == QStringLiteral("activate and raise"))
        return MouseActivateAndRaise;
    if (lowerName == QStringLiteral("activate and lower"))
        return MouseActivateAndLower;
    if (lowerName == QStringLiteral("activate"))
        return MouseActivate;
    if (lowerName == QStringLiteral("activate, raise and pass click"))
        return MouseActivateRaiseAndPassClick;
    if (lowerName == QStringLiteral("activate and pass click"))
        return MouseActivateAndPassClick;
    if (lowerName == QStringLiteral("scroll"))
        return MouseNothing;
    if (lowerName == QStringLiteral("activate and scroll"))
        return MouseActivateAndPassClick;
    if (lowerName == QStringLiteral("activate, raise and scroll"))
        return MouseActivateRaiseAndPassClick;
    if (lowerName == QStringLiteral("activate, raise and move"))
        return restricted ? MouseActivateRaiseAndMove : MouseActivateRaiseAndUnrestrictedMove;
    if (lowerName == QStringLiteral("move"))
        return restricted ? MouseMove : MouseUnrestrictedMove;
    if (lowerName == QStringLiteral("resize"))
        return restricted ? MouseResize : MouseUnrestrictedResize;
    if (lowerName == QStringLiteral("minimize"))
        return MouseMinimize;
    if (lowerName == QStringLiteral("close"))
        return MouseClose;
    if (lowerName == QStringLiteral("increase opacity"))
        return MouseOpacityMore;
    if (lowerName == QStringLiteral("decrease opacity"))
        return MouseOpacityLess;
    if (lowerName == QStringLiteral("nothing"))
        return MouseNothing;
    return MouseNothing;
}

options::MouseWheelCommand options::mouseWheelCommand(const QString& name)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise/lower"))
        return MouseWheelRaiseLower;
    if (lowerName == QStringLiteral("maximize/restore"))
        return MouseWheelMaximizeRestore;
    if (lowerName == QStringLiteral("above/below"))
        return MouseWheelAboveBelow;
    if (lowerName == QStringLiteral("previous/next desktop"))
        return MouseWheelPreviousNextDesktop;
    if (lowerName == QStringLiteral("change opacity"))
        return MouseWheelChangeOpacity;
    if (lowerName == QStringLiteral("nothing"))
        return MouseWheelNothing;
    return MouseWheelNothing;
}

bool options::condensedTitle() const
{
    return condensed_title;
}

options::MouseCommand options::wheelToMouseCommand(MouseWheelCommand com, int delta) const
{
    switch (com) {
    case MouseWheelRaiseLower:
        return delta > 0 ? MouseRaise : MouseLower;
    case MouseWheelMaximizeRestore:
        return delta > 0 ? MouseMaximize : MouseRestore;
    case MouseWheelAboveBelow:
        return delta > 0 ? MouseAbove : MouseBelow;
    case MouseWheelPreviousNextDesktop:
        return delta > 0 ? MousePreviousDesktop : MouseNextDesktop;
    case MouseWheelChangeOpacity:
        return delta > 0 ? MouseOpacityMore : MouseOpacityLess;
    default:
        return MouseNothing;
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

options::WindowOperation options::operationMaxButtonClick(Qt::MouseButtons button) const
{
    return button == Qt::RightButton ? opMaxButtonRightClick
        : button == Qt::MiddleButton ? opMaxButtonMiddleClick
                                     : opMaxButtonLeftClick;
}

QStringList options::modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const
{
    return m_modifierOnlyShortcuts.value(mod);
}

bool options::isUseCompositing() const
{
    return m_useCompositing || kwinApp()->get_base().render->requiresCompositing();
}

} // namespace
