/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "base/logging.h"

#include "config-kwin.h"
#include "utils/algorithm.h"
#include "win_settings.h"

#include <kwingl/platform.h>

#include <QOpenGLContext>
#include <QProcess>

namespace KWin::win
{

void options_qobject::setFocusPolicy(focus_policy focusPolicy)
{
    if (m_focusPolicy == focusPolicy) {
        return;
    }
    m_focusPolicy = focusPolicy;
    Q_EMIT focusPolicyChanged();
    if (m_focusPolicy == focus_policy::click) {
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
    if (m_focusPolicy == focus_policy::click) {
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
    if (m_focusPolicy == focus_policy::click) {
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
    if (m_focusPolicy == focus_policy::click) {
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

void options_qobject::setFocusStealingPreventionLevel(fsp_level lvl)
{
    if (!focusPolicyIsReasonable()) {
        lvl = fsp_level::none;
    }
    if (m_focusStealingPreventionLevel == lvl) {
        return;
    }

    if (enum_index(lvl) > enum_index(fsp_level::extreme)) {
        lvl = fsp_level::extreme;
    }
    if (enum_index(lvl) < enum_index(fsp_level::none)) {
        lvl = fsp_level::none;
    }

    m_focusStealingPreventionLevel = lvl;
    Q_EMIT focusStealingPreventionLevelChanged();
}

void options_qobject::setOperationTitlebarDblClick(win_op op)
{
    if (OpTitlebarDblClick == op) {
        return;
    }
    OpTitlebarDblClick = op;
    Q_EMIT operationTitlebarDblClickChanged();
}

void options_qobject::setOperationMaxButtonLeftClick(win_op op)
{
    if (opMaxButtonLeftClick == op) {
        return;
    }
    opMaxButtonLeftClick = op;
    Q_EMIT operationMaxButtonLeftClickChanged();
}

void options_qobject::setOperationMaxButtonRightClick(win_op op)
{
    if (opMaxButtonRightClick == op) {
        return;
    }
    opMaxButtonRightClick = op;
    Q_EMIT operationMaxButtonRightClickChanged();
}

void options_qobject::setOperationMaxButtonMiddleClick(win_op op)
{
    if (opMaxButtonMiddleClick == op) {
        return;
    }
    opMaxButtonMiddleClick = op;
    Q_EMIT operationMaxButtonMiddleClickChanged();
}

void options_qobject::setCommandActiveTitlebar1(mouse_cmd cmd)
{
    if (CmdActiveTitlebar1 == cmd) {
        return;
    }
    CmdActiveTitlebar1 = cmd;
    Q_EMIT commandActiveTitlebar1Changed();
}

void options_qobject::setCommandActiveTitlebar2(mouse_cmd cmd)
{
    if (CmdActiveTitlebar2 == cmd) {
        return;
    }
    CmdActiveTitlebar2 = cmd;
    Q_EMIT commandActiveTitlebar2Changed();
}

void options_qobject::setCommandActiveTitlebar3(mouse_cmd cmd)
{
    if (CmdActiveTitlebar3 == cmd) {
        return;
    }
    CmdActiveTitlebar3 = cmd;
    Q_EMIT commandActiveTitlebar3Changed();
}

void options_qobject::setCommandInactiveTitlebar1(mouse_cmd cmd)
{
    if (CmdInactiveTitlebar1 == cmd) {
        return;
    }
    CmdInactiveTitlebar1 = cmd;
    Q_EMIT commandInactiveTitlebar1Changed();
}

void options_qobject::setCommandInactiveTitlebar2(mouse_cmd cmd)
{
    if (CmdInactiveTitlebar2 == cmd) {
        return;
    }
    CmdInactiveTitlebar2 = cmd;
    Q_EMIT commandInactiveTitlebar2Changed();
}

void options_qobject::setCommandInactiveTitlebar3(mouse_cmd cmd)
{
    if (CmdInactiveTitlebar3 == cmd) {
        return;
    }
    CmdInactiveTitlebar3 = cmd;
    Q_EMIT commandInactiveTitlebar3Changed();
}

void options_qobject::setCommandWindow1(mouse_cmd cmd)
{
    if (CmdWindow1 == cmd) {
        return;
    }
    CmdWindow1 = cmd;
    Q_EMIT commandWindow1Changed();
}

void options_qobject::setCommandWindow2(mouse_cmd cmd)
{
    if (CmdWindow2 == cmd) {
        return;
    }
    CmdWindow2 = cmd;
    Q_EMIT commandWindow2Changed();
}

void options_qobject::setCommandWindow3(mouse_cmd cmd)
{
    if (CmdWindow3 == cmd) {
        return;
    }
    CmdWindow3 = cmd;
    Q_EMIT commandWindow3Changed();
}

void options_qobject::setCommandWindowWheel(mouse_cmd cmd)
{
    if (CmdWindowWheel == cmd) {
        return;
    }
    CmdWindowWheel = cmd;
    Q_EMIT commandWindowWheelChanged();
}

void options_qobject::setCommandAll1(mouse_cmd cmd)
{
    if (CmdAll1 == cmd) {
        return;
    }
    CmdAll1 = cmd;
    Q_EMIT commandAll1Changed();
}

void options_qobject::setCommandAll2(mouse_cmd cmd)
{
    if (CmdAll2 == cmd) {
        return;
    }
    CmdAll2 = cmd;
    Q_EMIT commandAll2Changed();
}

void options_qobject::setCommandAll3(mouse_cmd cmd)
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

void options::updateSettings()
{
    loadConfig();

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
        static_cast<fsp_level>(m_settings->focusStealingPreventionLevel()));

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
}

// restricted should be true for operations that the user may not be able to repeat
// if the window is moved out of the workspace (e.g. if the user moves a window
// by the titlebar, and moves it too high beneath Kicker at the top edge, they
// may not be able to move it back, unless they know about Meta+LMB)
win_op options::windowOperation(const QString& name, bool restricted)
{
    if (name == QStringLiteral("Move"))
        return restricted ? win_op::move : win_op::unrestricted_move;
    else if (name == QStringLiteral("Resize"))
        return restricted ? win_op::resize : win_op::unrestricted_resize;
    else if (name == QStringLiteral("Maximize"))
        return win_op::maximize;
    else if (name == QStringLiteral("Minimize"))
        return win_op::minimize;
    else if (name == QStringLiteral("Close"))
        return win_op::close;
    else if (name == QStringLiteral("OnAllDesktops"))
        return win_op::on_all_desktops;
    else if (name == QStringLiteral("Operations"))
        return win_op::operations;
    else if (name == QStringLiteral("Maximize (vertical only)"))
        return win_op::v_maximize;
    else if (name == QStringLiteral("Maximize (horizontal only)"))
        return win_op::h_maximize;
    else if (name == QStringLiteral("Lower"))
        return win_op::lower;
    return win_op::noop;
}

mouse_cmd options::mouseCommand(const QString& name, bool restricted)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise")) {
        return mouse_cmd::raise;
    }
    if (lowerName == QStringLiteral("lower")) {
        return mouse_cmd::lower;
    }
    if (lowerName == QStringLiteral("operations menu")) {
        return mouse_cmd::operations_menu;
    }
    if (lowerName == QStringLiteral("toggle raise and lower")) {
        return mouse_cmd::toggle_raise_and_lower;
    }
    if (lowerName == QStringLiteral("activate and raise")) {
        return mouse_cmd::activate_and_raise;
    }
    if (lowerName == QStringLiteral("activate and lower")) {
        return mouse_cmd::activate_and_lower;
    }
    if (lowerName == QStringLiteral("activate")) {
        return mouse_cmd::activate;
    }
    if (lowerName == QStringLiteral("activate, raise and pass click")) {
        return mouse_cmd::activate_raise_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate and pass click")) {
        return mouse_cmd::activate_and_pass_click;
    }
    if (lowerName == QStringLiteral("scroll")) {
        return mouse_cmd::nothing;
    }
    if (lowerName == QStringLiteral("activate and scroll")) {
        return mouse_cmd::activate_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate, raise and scroll")) {
        return mouse_cmd::activate_raise_and_pass_click;
    }
    if (lowerName == QStringLiteral("activate, raise and move")) {
        return restricted ? mouse_cmd::activate_raise_and_move
                          : mouse_cmd::activate_raise_and_unrestricted_move;
    }
    if (lowerName == QStringLiteral("move")) {
        return restricted ? mouse_cmd::move : mouse_cmd::unrestricted_move;
    }
    if (lowerName == QStringLiteral("resize")) {
        return restricted ? mouse_cmd::resize : mouse_cmd::unrestricted_resize;
    }
    if (lowerName == QStringLiteral("minimize")) {
        return mouse_cmd::minimize;
    }
    if (lowerName == QStringLiteral("close")) {
        return mouse_cmd::close;
    }
    if (lowerName == QStringLiteral("increase opacity")) {
        return mouse_cmd::opacity_more;
    }
    if (lowerName == QStringLiteral("decrease opacity")) {
        return mouse_cmd::opacity_less;
    }
    if (lowerName == QStringLiteral("nothing")) {
        return mouse_cmd::nothing;
    }
    return mouse_cmd::nothing;
}

mouse_wheel_cmd options::mouseWheelCommand(const QString& name)
{
    QString lowerName = name.toLower();
    if (lowerName == QStringLiteral("raise/lower"))
        return mouse_wheel_cmd::raise_lower;
    if (lowerName == QStringLiteral("maximize/restore"))
        return mouse_wheel_cmd::maximize_restore;
    if (lowerName == QStringLiteral("above/below"))
        return mouse_wheel_cmd::above_below;
    if (lowerName == QStringLiteral("previous/next desktop"))
        return mouse_wheel_cmd::previous_next_desktop;
    if (lowerName == QStringLiteral("change opacity"))
        return mouse_wheel_cmd::change_opacity;
    if (lowerName == QStringLiteral("nothing"))
        return mouse_wheel_cmd::nothing;
    return mouse_wheel_cmd::nothing;
}

bool options_qobject::condensedTitle() const
{
    return condensed_title;
}

mouse_cmd options::wheelToMouseCommand(mouse_wheel_cmd com, int delta) const
{
    switch (com) {
    case mouse_wheel_cmd::raise_lower:
        return delta > 0 ? mouse_cmd::raise : mouse_cmd::lower;
    case mouse_wheel_cmd::maximize_restore:
        return delta > 0 ? mouse_cmd::maximize : mouse_cmd::restore;
    case mouse_wheel_cmd::above_below:
        return delta > 0 ? mouse_cmd::above : mouse_cmd::below;
    case mouse_wheel_cmd::previous_next_desktop:
        return delta > 0 ? mouse_cmd::previous_desktop : mouse_cmd::next_desktop;
    case mouse_wheel_cmd::change_opacity:
        return delta > 0 ? mouse_cmd::opacity_more : mouse_cmd::opacity_less;
    default:
        return mouse_cmd::nothing;
    }
}

win_op options_qobject::operationMaxButtonClick(Qt::MouseButtons button) const
{
    return button == Qt::RightButton ? opMaxButtonRightClick
        : button == Qt::MiddleButton ? opMaxButtonMiddleClick
                                     : opMaxButtonLeftClick;
}

options::options(KSharedConfigPtr config)
    : qobject{std::make_unique<options_qobject>()}
    , m_settings(new settings(config))
{
    m_settings->setDefaults();
    syncFromKcfgc();

    m_configWatcher = KConfigWatcher::create(m_settings->sharedConfig());

    loadConfig();
}

options::~options() = default;

}
