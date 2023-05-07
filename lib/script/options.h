/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "win/types.h"

#include <QObject>

namespace KWin
{

namespace base
{
class options;
}

namespace win
{
class options;
}

namespace scripting
{

class KWIN_EXPORT options : public QObject
{
    Q_OBJECT

    Q_PROPERTY(
        FocusPolicy focusPolicy READ focusPolicy WRITE setFocusPolicy NOTIFY focusPolicyChanged)
    Q_PROPERTY(AnimationCurve animationCurve READ animationCurve WRITE setAnimationCurve NOTIFY
                   animationCurveChanged)
    Q_PROPERTY(bool nextFocusPrefersMouse READ isNextFocusPrefersMouse WRITE
                   setNextFocusPrefersMouse NOTIFY nextFocusPrefersMouseChanged)
    /**
     * Whether clicking on a window raises it in FocusFollowsMouse
     * mode or not.
     */
    Q_PROPERTY(bool clickRaise READ isClickRaise WRITE setClickRaise NOTIFY clickRaiseChanged)
    /**
     * Whether autoraise is enabled FocusFollowsMouse mode or not.
     */
    Q_PROPERTY(bool autoRaise READ isAutoRaise WRITE setAutoRaise NOTIFY autoRaiseChanged)
    /**
     * Autoraise interval.
     */
    Q_PROPERTY(int autoRaiseInterval READ autoRaiseInterval WRITE setAutoRaiseInterval NOTIFY
                   autoRaiseIntervalChanged)
    /**
     * Delayed focus interval.
     */
    Q_PROPERTY(int delayFocusInterval READ delayFocusInterval WRITE setDelayFocusInterval NOTIFY
                   delayFocusIntervalChanged)
    /**
     * Whether to see Xinerama screens separately for focus (in Alt+Tab, when activating next
     * client)
     */
    Q_PROPERTY(bool separateScreenFocus READ isSeparateScreenFocus WRITE setSeparateScreenFocus
                   NOTIFY separateScreenFocusChanged)
    Q_PROPERTY(
        KWin::win::placement placement READ placement WRITE setPlacement NOTIFY placementChanged)
    Q_PROPERTY(bool focusPolicyIsReasonable READ focusPolicyIsReasonable NOTIFY
                   focusPolicyIsResonableChanged)
    /**
     * The size of the zone that triggers snapping on desktop borders.
     */
    Q_PROPERTY(
        int borderSnapZone READ borderSnapZone WRITE setBorderSnapZone NOTIFY borderSnapZoneChanged)
    /**
     * The size of the zone that triggers snapping with other windows.
     */
    Q_PROPERTY(
        int windowSnapZone READ windowSnapZone WRITE setWindowSnapZone NOTIFY windowSnapZoneChanged)
    /**
     * The size of the zone that triggers snapping on the screen center.
     */
    Q_PROPERTY(
        int centerSnapZone READ centerSnapZone WRITE setCenterSnapZone NOTIFY centerSnapZoneChanged)
    /**
     * Snap only when windows will overlap.
     */
    Q_PROPERTY(bool snapOnlyWhenOverlapping READ isSnapOnlyWhenOverlapping WRITE
                   setSnapOnlyWhenOverlapping NOTIFY snapOnlyWhenOverlappingChanged)
    /**
     * Whether or not we roll over to the other edge when switching desktops past the edge.
     */
    Q_PROPERTY(bool rollOverDesktops READ isRollOverDesktops WRITE setRollOverDesktops NOTIFY
                   rollOverDesktopsChanged)
    /**
     * 0 - 4 , see Workspace::allowClientActivation()
     */
    Q_PROPERTY(win::fsp_level focusStealingPreventionLevel READ focusStealingPreventionLevel WRITE
                   setFocusStealingPreventionLevel NOTIFY focusStealingPreventionLevelChanged)
    Q_PROPERTY(WindowOperation operationTitlebarDblClick READ operationTitlebarDblClick WRITE
                   setOperationTitlebarDblClick NOTIFY operationTitlebarDblClickChanged)
    Q_PROPERTY(WindowOperation operationMaxButtonLeftClick READ operationMaxButtonLeftClick WRITE
                   setOperationMaxButtonLeftClick NOTIFY operationMaxButtonLeftClickChanged)
    Q_PROPERTY(
        WindowOperation operationMaxButtonMiddleClick READ operationMaxButtonMiddleClick WRITE
            setOperationMaxButtonMiddleClick NOTIFY operationMaxButtonMiddleClickChanged)
    Q_PROPERTY(WindowOperation operationMaxButtonRightClick READ operationMaxButtonRightClick WRITE
                   setOperationMaxButtonRightClick NOTIFY operationMaxButtonRightClickChanged)
    Q_PROPERTY(MouseCommand commandActiveTitlebar1 READ commandActiveTitlebar1 WRITE
                   setCommandActiveTitlebar1 NOTIFY commandActiveTitlebar1Changed)
    Q_PROPERTY(MouseCommand commandActiveTitlebar2 READ commandActiveTitlebar2 WRITE
                   setCommandActiveTitlebar2 NOTIFY commandActiveTitlebar2Changed)
    Q_PROPERTY(MouseCommand commandActiveTitlebar3 READ commandActiveTitlebar3 WRITE
                   setCommandActiveTitlebar3 NOTIFY commandActiveTitlebar3Changed)
    Q_PROPERTY(MouseCommand commandInactiveTitlebar1 READ commandInactiveTitlebar1 WRITE
                   setCommandInactiveTitlebar1 NOTIFY commandInactiveTitlebar1Changed)
    Q_PROPERTY(MouseCommand commandInactiveTitlebar2 READ commandInactiveTitlebar2 WRITE
                   setCommandInactiveTitlebar2 NOTIFY commandInactiveTitlebar2Changed)
    Q_PROPERTY(MouseCommand commandInactiveTitlebar3 READ commandInactiveTitlebar3 WRITE
                   setCommandInactiveTitlebar3 NOTIFY commandInactiveTitlebar3Changed)
    Q_PROPERTY(MouseCommand commandWindow1 READ commandWindow1 WRITE setCommandWindow1 NOTIFY
                   commandWindow1Changed)
    Q_PROPERTY(MouseCommand commandWindow2 READ commandWindow2 WRITE setCommandWindow2 NOTIFY
                   commandWindow2Changed)
    Q_PROPERTY(MouseCommand commandWindow3 READ commandWindow3 WRITE setCommandWindow3 NOTIFY
                   commandWindow3Changed)
    Q_PROPERTY(MouseCommand commandWindowWheel READ commandWindowWheel WRITE setCommandWindowWheel
                   NOTIFY commandWindowWheelChanged)
    Q_PROPERTY(
        MouseCommand commandAll1 READ commandAll1 WRITE setCommandAll1 NOTIFY commandAll1Changed)
    Q_PROPERTY(
        MouseCommand commandAll2 READ commandAll2 WRITE setCommandAll2 NOTIFY commandAll2Changed)
    Q_PROPERTY(
        MouseCommand commandAll3 READ commandAll3 WRITE setCommandAll3 NOTIFY commandAll3Changed)
    Q_PROPERTY(uint keyCmdAllModKey READ keyCmdAllModKey WRITE setKeyCmdAllModKey NOTIFY
                   keyCmdAllModKeyChanged)
    /**
     * Whether the visible name should be condensed.
     */
    Q_PROPERTY(bool condensedTitle READ condensedTitle WRITE setCondensedTitle NOTIFY
                   condensedTitleChanged)
    /**
     * Whether a window gets maximized when it reaches top screen edge while being moved.
     */
    Q_PROPERTY(bool electricBorderMaximize READ electricBorderMaximize WRITE
                   setElectricBorderMaximize NOTIFY electricBorderMaximizeChanged)
    /**
     * Whether a window is tiled to half screen when reaching left or right screen edge while been
     * moved.
     */
    Q_PROPERTY(bool electricBorderTiling READ electricBorderTiling WRITE setElectricBorderTiling
                   NOTIFY electricBorderTilingChanged)
    /**
     * Whether a window is tiled to half screen when reaching left or right screen edge while been
     * moved.
     */
    Q_PROPERTY(float electricBorderCornerRatio READ electricBorderCornerRatio WRITE
                   setElectricBorderCornerRatio NOTIFY electricBorderCornerRatioChanged)
    Q_PROPERTY(bool borderlessMaximizedWindows READ borderlessMaximizedWindows WRITE
                   setBorderlessMaximizedWindows NOTIFY borderlessMaximizedWindowsChanged)
    /**
     * timeout before non-responding application will be killed after attempt to close.
     */
    Q_PROPERTY(int killPingTimeout READ killPingTimeout WRITE setKillPingTimeout NOTIFY
                   killPingTimeoutChanged)
    /**
     * Whether to hide utility windows for inactive applications.
     */
    Q_PROPERTY(bool hideUtilityWindowsForInactive READ isHideUtilityWindowsForInactive WRITE
                   setHideUtilityWindowsForInactive NOTIFY hideUtilityWindowsForInactiveChanged)
    Q_PROPERTY(int compositingMode READ compositingMode WRITE setCompositingMode NOTIFY
                   compositingModeChanged)
    Q_PROPERTY(bool useCompositing READ isUseCompositing WRITE setUseCompositing NOTIFY
                   useCompositingChanged)
    Q_PROPERTY(
        int hiddenPreviews READ hiddenPreviews WRITE setHiddenPreviews NOTIFY hiddenPreviewsChanged)
    Q_PROPERTY(qint64 maxFpsInterval READ maxFpsInterval WRITE setMaxFpsInterval NOTIFY
                   maxFpsIntervalChanged)
    Q_PROPERTY(uint refreshRate READ refreshRate WRITE setRefreshRate NOTIFY refreshRateChanged)
    Q_PROPERTY(qint64 vBlankTime READ vBlankTime WRITE setVBlankTime NOTIFY vBlankTimeChanged)
    Q_PROPERTY(bool glStrictBinding READ isGlStrictBinding WRITE setGlStrictBinding NOTIFY
                   glStrictBindingChanged)
    /**
     * Whether strict binding follows the driver or has been overwritten by a user defined config
     * value. If @c true glStrictBinding is set by the OpenGL Scene during initialization. If @c
     * false glStrictBinding is set from a config value and not updated during scene initialization.
     */
    Q_PROPERTY(bool glStrictBindingFollowsDriver READ isGlStrictBindingFollowsDriver WRITE
                   setGlStrictBindingFollowsDriver NOTIFY glStrictBindingFollowsDriverChanged)

    /// Deprecated
    Q_PROPERTY(bool windowsBlockCompositing READ windowsBlockCompositing WRITE
                   setWindowsBlockCompositing NOTIFY windowsBlockCompositingChanged)
public:
    options(base::options& base, win::options& win);

    /**
     * This enum type is used to specify the focus policy.
     *
     * Note that FocusUnderMouse and FocusStrictlyUnderMouse are not
     * particulary useful. They are only provided for old-fashined
     * die-hard UNIX people ;-)
     */
    enum FocusPolicy {
        /**
         * Clicking into a window activates it. This is also the default.
         */
        ClickToFocus,
        /**
         * Moving the mouse pointer actively onto a normal window activates it.
         * For convenience, the desktop and windows on the dock are excluded.
         * They require clicking.
         */
        FocusFollowsMouse,
        /**
         * The window that happens to be under the mouse pointer becomes active.
         * The invariant is: no window can have focus that is not under the mouse.
         * This also means that Alt-Tab won't work properly and popup dialogs are
         * usually unsable with the keyboard. Note that the desktop and windows on
         * the dock are excluded for convenience. They get focus only when clicking
         * on it.
         */
        FocusUnderMouse,
        /**
         * This is even worse than FocusUnderMouse. Only the window under the mouse
         * pointer is active. If the mouse points nowhere, nothing has the focus. If
         * the mouse points onto the desktop, the desktop has focus. The same holds
         * for windows on the dock.
         */
        FocusStrictlyUnderMouse
    };
    Q_ENUM(FocusPolicy)

    enum WindowOperation {
        MaximizeOp = 5000,
        RestoreOp,
        MinimizeOp,
        MoveOp,
        UnrestrictedMoveOp,
        ResizeOp,
        UnrestrictedResizeOp,
        CloseOp,
        OnAllDesktopsOp,
        KeepAboveOp,
        KeepBelowOp,
        OperationsOp,
        WindowRulesOp,
        ToggleStoreSettingsOp = WindowRulesOp, ///< @obsolete
        HMaximizeOp,
        VMaximizeOp,
        LowerOp,
        FullScreenOp,
        NoBorderOp,
        NoOp,
        SetupWindowShortcutOp,
        ApplicationRulesOp,
    };
    Q_ENUM(WindowOperation)

    enum MouseCommand {
        MouseRaise,
        MouseLower,
        MouseOperationsMenu,
        MouseToggleRaiseAndLower,
        MouseActivateAndRaise,
        MouseActivateAndLower,
        MouseActivate,
        MouseActivateRaiseAndPassClick,
        MouseActivateAndPassClick,
        MouseMove,
        MouseUnrestrictedMove,
        MouseActivateRaiseAndMove,
        MouseActivateRaiseAndUnrestrictedMove,
        MouseResize,
        MouseUnrestrictedResize,
        MouseMaximize,
        MouseRestore,
        MouseMinimize,
        MouseNextDesktop,
        MousePreviousDesktop,
        MouseAbove,
        MouseBelow,
        MouseOpacityMore,
        MouseOpacityLess,
        MouseClose,
        MouseNothing
    };
    Q_ENUM(MouseCommand)

    enum MouseWheelCommand {
        MouseWheelRaiseLower,
        MouseWheelMaximizeRestore,
        MouseWheelAboveBelow,
        MouseWheelPreviousNextDesktop,
        MouseWheelChangeOpacity,
        MouseWheelNothing
    };
    Q_ENUM(MouseWheelCommand)

    enum AnimationCurve {
        Linear,
        Quadratic,
        Cubic,
        Quartic,
        Sine,
    };
    Q_ENUM(AnimationCurve)

    FocusPolicy focusPolicy() const;
    bool isNextFocusPrefersMouse() const;
    bool isClickRaise() const;
    bool isAutoRaise() const;
    int autoRaiseInterval() const;
    int delayFocusInterval() const;
    bool isSeparateScreenFocus() const;

    win::placement placement() const;
    bool focusPolicyIsReasonable();
    int borderSnapZone() const;
    int windowSnapZone() const;
    int centerSnapZone() const;
    bool isSnapOnlyWhenOverlapping() const;

    bool isRollOverDesktops() const;
    win::fsp_level focusStealingPreventionLevel() const;

    WindowOperation operationTitlebarDblClick() const;
    WindowOperation operationMaxButtonLeftClick() const;
    WindowOperation operationMaxButtonRightClick() const;
    WindowOperation operationMaxButtonMiddleClick() const;
    WindowOperation operationMaxButtonClick(Qt::MouseButtons button) const;

    MouseCommand commandActiveTitlebar1() const;
    MouseCommand commandActiveTitlebar2() const;
    MouseCommand commandActiveTitlebar3() const;
    MouseCommand commandInactiveTitlebar1() const;
    MouseCommand commandInactiveTitlebar2() const;
    MouseCommand commandInactiveTitlebar3() const;
    MouseCommand commandWindow1() const;
    MouseCommand commandWindow2() const;
    MouseCommand commandWindow3() const;
    MouseCommand commandWindowWheel() const;
    MouseCommand commandAll1() const;
    MouseCommand commandAll2() const;
    MouseCommand commandAll3() const;
    MouseWheelCommand commandAllWheel() const;
    uint keyCmdAllModKey() const;
    Qt::KeyboardModifier commandAllModifier() const;

    bool condensedTitle() const;

    bool electricBorderMaximize() const;
    bool electricBorderTiling() const;
    float electricBorderCornerRatio() const;
    bool borderlessMaximizedWindows() const;

    int killPingTimeout() const;
    bool isHideUtilityWindowsForInactive() const;
    int hiddenPreviews() const;

    int compositingMode() const;
    void setCompositingMode(int mode);
    bool isUseCompositing() const;

    qint64 maxFpsInterval() const;
    uint refreshRate() const;
    qint64 vBlankTime() const;

    bool isGlStrictBinding() const;
    bool isGlStrictBindingFollowsDriver() const;

    bool windowsBlockCompositing() const;
    AnimationCurve animationCurve() const;

    // setters
    void setFocusPolicy(FocusPolicy focusPolicy);
    void setNextFocusPrefersMouse(bool nextFocusPrefersMouse);
    void setClickRaise(bool clickRaise);
    void setAutoRaise(bool autoRaise);
    void setAutoRaiseInterval(int autoRaiseInterval);
    void setDelayFocusInterval(int delayFocusInterval);
    void setSeparateScreenFocus(bool separateScreenFocus);
    void setPlacement(win::placement placement);
    void setBorderSnapZone(int borderSnapZone);
    void setWindowSnapZone(int windowSnapZone);
    void setCenterSnapZone(int centerSnapZone);
    void setSnapOnlyWhenOverlapping(bool snapOnlyWhenOverlapping);
    void setRollOverDesktops(bool rollOverDesktops);
    void setFocusStealingPreventionLevel(win::fsp_level lvl);
    void setOperationTitlebarDblClick(WindowOperation op);
    void setOperationMaxButtonLeftClick(WindowOperation op);
    void setOperationMaxButtonRightClick(WindowOperation op);
    void setOperationMaxButtonMiddleClick(WindowOperation op);
    void setCommandActiveTitlebar1(MouseCommand cmd);
    void setCommandActiveTitlebar2(MouseCommand cmd);
    void setCommandActiveTitlebar3(MouseCommand cmd);
    void setCommandInactiveTitlebar1(MouseCommand cmd);
    void setCommandInactiveTitlebar2(MouseCommand cmd);
    void setCommandInactiveTitlebar3(MouseCommand cmd);
    void setCommandWindow1(MouseCommand cmd);
    void setCommandWindow2(MouseCommand cmd);
    void setCommandWindow3(MouseCommand cmd);
    void setCommandWindowWheel(MouseCommand cmd);
    void setCommandAll1(MouseCommand cmd);
    void setCommandAll2(MouseCommand cmd);
    void setCommandAll3(MouseCommand cmd);
    void setKeyCmdAllModKey(uint keyCmdAllModKey);
    void setCondensedTitle(bool condensedTitle);
    void setElectricBorderMaximize(bool electricBorderMaximize);
    void setElectricBorderTiling(bool electricBorderTiling);
    void setElectricBorderCornerRatio(float electricBorderCornerRatio);
    void setBorderlessMaximizedWindows(bool borderlessMaximizedWindows);
    void setKillPingTimeout(int killPingTimeout);
    void setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive);
    void setUseCompositing(bool useCompositing);
    void setHiddenPreviews(int set);
    void setMaxFpsInterval(qint64 maxFpsInterval);
    void setRefreshRate(uint refreshRate);
    void setVBlankTime(qint64 vBlankTime);
    void setGlStrictBinding(bool glStrictBinding);
    void setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver);
    void setWindowsBlockCompositing(bool set);
    void setAnimationCurve(AnimationCurve curve);

Q_SIGNALS:
    // for properties
    void focusPolicyChanged();
    void focusPolicyIsResonableChanged();
    void nextFocusPrefersMouseChanged();
    void clickRaiseChanged();
    void autoRaiseChanged();
    void autoRaiseIntervalChanged();
    void delayFocusIntervalChanged();
    void separateScreenFocusChanged(bool);
    void placementChanged();
    void borderSnapZoneChanged();
    void windowSnapZoneChanged();
    void centerSnapZoneChanged();
    void snapOnlyWhenOverlappingChanged();
    void rollOverDesktopsChanged(bool enabled);
    void focusStealingPreventionLevelChanged();
    void operationTitlebarDblClickChanged();
    void operationMaxButtonLeftClickChanged();
    void operationMaxButtonRightClickChanged();
    void operationMaxButtonMiddleClickChanged();
    void commandActiveTitlebar1Changed();
    void commandActiveTitlebar2Changed();
    void commandActiveTitlebar3Changed();
    void commandInactiveTitlebar1Changed();
    void commandInactiveTitlebar2Changed();
    void commandInactiveTitlebar3Changed();
    void commandWindow1Changed();
    void commandWindow2Changed();
    void commandWindow3Changed();
    void commandWindowWheelChanged();
    void commandAll1Changed();
    void commandAll2Changed();
    void commandAll3Changed();
    void keyCmdAllModKeyChanged();
    void condensedTitleChanged();
    void electricBorderMaximizeChanged();
    void electricBorderTilingChanged();
    void electricBorderCornerRatioChanged();
    void borderlessMaximizedWindowsChanged();
    void killPingTimeoutChanged();
    void hideUtilityWindowsForInactiveChanged();
    void compositingModeChanged();
    void useCompositingChanged();
    void hiddenPreviewsChanged();
    void maxFpsIntervalChanged();
    void refreshRateChanged();
    void vBlankTimeChanged();
    void glStrictBindingChanged();
    void glStrictBindingFollowsDriverChanged();

    void windowsBlockCompositingChanged();
    void animationSpeedChanged();
    void animationCurveChanged();

    void configChanged();

private:
    base::options& base;
    win::options& win;
};

}
}

Q_DECLARE_METATYPE(KWin::win::fsp_level)
Q_DECLARE_METATYPE(KWin::win::placement)
