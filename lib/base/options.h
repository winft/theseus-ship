/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "kwin_export.h"
#include "kwinglobals.h"
#include "render/types.h"
#include "render/x11/types.h"
#include "win/types.h"

#include <KConfigWatcher>

namespace KWin::base
{

KWIN_EXPORT OpenGLPlatformInterface defaultGlPlatformInterface(operation_mode mode);

class Settings;

class KWIN_EXPORT options_qobject : public QObject
{
    Q_OBJECT

public:
    options_qobject(base::operation_mode mode);

    win::focus_policy focusPolicy() const
    {
        return m_focusPolicy;
    }
    bool isNextFocusPrefersMouse() const
    {
        return m_nextFocusPrefersMouse;
    }

    /**
     * Whether clicking on a window raises it in FocusFollowsMouse
     * mode or not.
     */
    bool isClickRaise() const
    {
        return m_clickRaise;
    }

    /**
     * Whether autoraise is enabled FocusFollowsMouse mode or not.
     */
    bool isAutoRaise() const
    {
        return m_autoRaise;
    }

    /**
     * Autoraise interval
     */
    int autoRaiseInterval() const
    {
        return m_autoRaiseInterval;
    }

    /**
     * Delayed focus interval.
     */
    int delayFocusInterval() const
    {
        return m_delayFocusInterval;
    }

    /**
     * Whether to see Xinerama screens separately for focus (in Alt+Tab, when activating next
     * client)
     */
    bool isSeparateScreenFocus() const
    {
        return m_separateScreenFocus;
    }

    win::placement placement() const
    {
        return m_placement;
    }

    bool focusPolicyIsReasonable()
    {
        return m_focusPolicy == win::focus_policy::click
            || m_focusPolicy == win::focus_policy::follows_mouse;
    }

    /**
     * The size of the zone that triggers snapping on desktop borders.
     */
    int borderSnapZone() const
    {
        return m_borderSnapZone;
    }

    /**
     * The size of the zone that triggers snapping with other windows.
     */
    int windowSnapZone() const
    {
        return m_windowSnapZone;
    }

    /**
     * The size of the zone that triggers snapping on the screen center.
     */
    int centerSnapZone() const
    {
        return m_centerSnapZone;
    }

    /**
     * Snap only when windows will overlap.
     */
    bool isSnapOnlyWhenOverlapping() const
    {
        return m_snapOnlyWhenOverlapping;
    }

    /**
     * Whether or not we roll over to the other edge when switching desktops past the edge.
     */
    bool isRollOverDesktops() const
    {
        return m_rollOverDesktops;
    }

    /**
     * Returns the focus stealing prevention level.
     *
     * @see allowClientActivation
     */
    win::fsp_level focusStealingPreventionLevel() const
    {
        return m_focusStealingPreventionLevel;
    }

    win::win_op operationTitlebarDblClick() const
    {
        return OpTitlebarDblClick;
    }
    win::win_op operationMaxButtonLeftClick() const
    {
        return opMaxButtonLeftClick;
    }
    win::win_op operationMaxButtonRightClick() const
    {
        return opMaxButtonRightClick;
    }
    win::win_op operationMaxButtonMiddleClick() const
    {
        return opMaxButtonMiddleClick;
    }
    win::win_op operationMaxButtonClick(Qt::MouseButtons button) const;

    win::mouse_cmd commandActiveTitlebar1() const
    {
        return CmdActiveTitlebar1;
    }
    win::mouse_cmd commandActiveTitlebar2() const
    {
        return CmdActiveTitlebar2;
    }
    win::mouse_cmd commandActiveTitlebar3() const
    {
        return CmdActiveTitlebar3;
    }
    win::mouse_cmd commandInactiveTitlebar1() const
    {
        return CmdInactiveTitlebar1;
    }
    win::mouse_cmd commandInactiveTitlebar2() const
    {
        return CmdInactiveTitlebar2;
    }
    win::mouse_cmd commandInactiveTitlebar3() const
    {
        return CmdInactiveTitlebar3;
    }
    win::mouse_cmd commandWindow1() const
    {
        return CmdWindow1;
    }
    win::mouse_cmd commandWindow2() const
    {
        return CmdWindow2;
    }
    win::mouse_cmd commandWindow3() const
    {
        return CmdWindow3;
    }
    win::mouse_cmd commandWindowWheel() const
    {
        return CmdWindowWheel;
    }
    win::mouse_cmd commandAll1() const
    {
        return CmdAll1;
    }
    win::mouse_cmd commandAll2() const
    {
        return CmdAll2;
    }
    win::mouse_cmd commandAll3() const
    {
        return CmdAll3;
    }
    win::mouse_wheel_cmd commandAllWheel() const
    {
        return CmdAllWheel;
    }
    uint keyCmdAllModKey() const
    {
        return CmdAllModKey;
    }
    Qt::KeyboardModifier commandAllModifier() const
    {
        switch (CmdAllModKey) {
        case Qt::Key_Alt:
            return Qt::AltModifier;
        case Qt::Key_Meta:
            return Qt::MetaModifier;
        default:
            Q_UNREACHABLE();
        }
    }

    /**
     * Returns whether the user prefers his caption clean.
     */
    bool condensedTitle() const;

    /**
     * @returns true if a window gets maximized when it reaches top screen edge
     * while being moved.
     */
    bool electricBorderMaximize() const
    {
        return electric_border_maximize;
    }
    /**
     * @returns true if window is tiled to half screen when reaching left or
     * right screen edge while been moved.
     */
    bool electricBorderTiling() const
    {
        return electric_border_tiling;
    }
    /**
     * @returns the factor that determines the corner part of the edge (ie. 0.1 means tiny corner)
     */
    float electricBorderCornerRatio() const
    {
        return electric_border_corner_ratio;
    }

    bool borderlessMaximizedWindows() const
    {
        return borderless_maximized_windows;
    }

    /**
     * Timeout before non-responding application will be killed after attempt to close.
     */
    int killPingTimeout() const
    {
        return m_killPingTimeout;
    }

    /**
     * Whether to hide utility windows for inactive applications.
     */
    bool isHideUtilityWindowsForInactive() const
    {
        return m_hideUtilityWindowsForInactive;
    }

    //----------------------
    // Compositing settings
    CompositingType compositingMode() const
    {
        return m_compositingMode;
    }
    void setCompositingMode(CompositingType mode)
    {
        m_compositingMode = mode;
    }
    // Separate to mode so the user can toggle
    bool isUseCompositing() const;

    // General preferences
    render::x11::hidden_preview hiddenPreviews() const
    {
        return m_hiddenPreviews;
    }

    qint64 maxFpsInterval() const
    {
        return m_maxFpsInterval;
    }
    // Settings that should be auto-detected
    uint refreshRate() const
    {
        return m_refreshRate;
    }
    qint64 vBlankTime() const
    {
        return m_vBlankTime;
    }
    bool isGlStrictBinding() const
    {
        return m_glStrictBinding;
    }
    bool isGlStrictBindingFollowsDriver() const
    {
        return m_glStrictBindingFollowsDriver;
    }

    /// Deprecated
    OpenGLPlatformInterface glPlatformInterface() const
    {
        return defaultGlPlatformInterface(windowing_mode);
    }

    bool windowsBlockCompositing() const
    {
        return m_windowsBlockCompositing;
    }

    render::animation_curve animationCurve() const
    {
        return m_animationCurve;
    }

    // setters
    void setFocusPolicy(win::focus_policy focusPolicy);
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
    void setFocusStealingPreventionLevel(win::fsp_level focusStealingPreventionLevel);
    void setOperationTitlebarDblClick(win::win_op op);
    void setOperationMaxButtonLeftClick(win::win_op op);
    void setOperationMaxButtonRightClick(win::win_op op);
    void setOperationMaxButtonMiddleClick(win::win_op op);
    void setCommandActiveTitlebar1(win::mouse_cmd cmd);
    void setCommandActiveTitlebar2(win::mouse_cmd cmd);
    void setCommandActiveTitlebar3(win::mouse_cmd cmd);
    void setCommandInactiveTitlebar1(win::mouse_cmd cmd);
    void setCommandInactiveTitlebar2(win::mouse_cmd cmd);
    void setCommandInactiveTitlebar3(win::mouse_cmd cmd);
    void setCommandWindow1(win::mouse_cmd cmd);
    void setCommandWindow2(win::mouse_cmd cmd);
    void setCommandWindow3(win::mouse_cmd cmd);
    void setCommandWindowWheel(win::mouse_cmd cmd);
    void setCommandAll1(win::mouse_cmd cmd);
    void setCommandAll2(win::mouse_cmd cmd);
    void setCommandAll3(win::mouse_cmd cmd);
    void setKeyCmdAllModKey(uint keyCmdAllModKey);
    void setCondensedTitle(bool condensedTitle);
    void setElectricBorderMaximize(bool electricBorderMaximize);
    void setElectricBorderTiling(bool electricBorderTiling);
    void setElectricBorderCornerRatio(float electricBorderCornerRatio);
    void setBorderlessMaximizedWindows(bool borderlessMaximizedWindows);
    void setKillPingTimeout(int killPingTimeout);
    void setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive);
    void setCompositingMode(int compositingMode);
    void setUseCompositing(bool useCompositing);
    void setHiddenPreviews(render::x11::hidden_preview hiddenPreviews);
    void setMaxFpsInterval(qint64 maxFpsInterval);
    void setRefreshRate(uint refreshRate);
    void setVBlankTime(qint64 vBlankTime);
    void setGlStrictBinding(bool glStrictBinding);
    void setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver);

    /// Deprecated
    void setGlPlatformInterface(OpenGLPlatformInterface /*interface*/)
    {
    }
    void setWindowsBlockCompositing(bool set);
    void setAnimationCurve(render::animation_curve curve);

    // default values
    static win::win_op defaultOperationTitlebarDblClick()
    {
        return win::win_op::maximize;
    }
    static win::win_op defaultOperationMaxButtonLeftClick()
    {
        return win::win_op::maximize;
    }
    static win::win_op defaultOperationMaxButtonRightClick()
    {
        return win::win_op::h_maximize;
    }
    static win::win_op defaultOperationMaxButtonMiddleClick()
    {
        return win::win_op::v_maximize;
    }
    static win::mouse_cmd defaultCommandActiveTitlebar1()
    {
        return win::mouse_cmd::raise;
    }
    static win::mouse_cmd defaultCommandActiveTitlebar2()
    {
        return win::mouse_cmd::nothing;
    }
    static win::mouse_cmd defaultCommandActiveTitlebar3()
    {
        return win::mouse_cmd::operations_menu;
    }
    static win::mouse_cmd defaultCommandInactiveTitlebar1()
    {
        return win::mouse_cmd::activate_and_raise;
    }
    static win::mouse_cmd defaultCommandInactiveTitlebar2()
    {
        return win::mouse_cmd::nothing;
    }
    static win::mouse_cmd defaultCommandInactiveTitlebar3()
    {
        return win::mouse_cmd::operations_menu;
    }
    static win::mouse_cmd defaultCommandWindow1()
    {
        return win::mouse_cmd::activate_raise_and_pass_click;
    }
    static win::mouse_cmd defaultCommandWindow2()
    {
        return win::mouse_cmd::activate_and_pass_click;
    }
    static win::mouse_cmd defaultCommandWindow3()
    {
        return win::mouse_cmd::activate_and_pass_click;
    }
    static win::mouse_cmd defaultCommandWindowWheel()
    {
        return win::mouse_cmd::nothing;
    }
    static win::mouse_cmd defaultCommandAll1()
    {
        return win::mouse_cmd::unrestricted_move;
    }
    static win::mouse_cmd defaultCommandAll2()
    {
        return win::mouse_cmd::toggle_raise_and_lower;
    }
    static win::mouse_cmd defaultCommandAll3()
    {
        return win::mouse_cmd::unrestricted_resize;
    }
    static win::mouse_wheel_cmd defaultCommandTitlebarWheel()
    {
        return win::mouse_wheel_cmd::nothing;
    }
    static win::mouse_wheel_cmd defaultCommandAllWheel()
    {
        return win::mouse_wheel_cmd::nothing;
    }
    static uint defaultKeyCmdAllModKey()
    {
        return Qt::Key_Alt;
    }
    static CompositingType defaultCompositingMode()
    {
        return OpenGLCompositing;
    }
    static bool defaultUseCompositing()
    {
        return true;
    }
    static render::x11::hidden_preview defaultHiddenPreviews()
    {
        return render::x11::hidden_preview::shown;
    }
    static qint64 defaultMaxFpsInterval()
    {
        return (1 * 1000 * 1000 * 1000) / 60.0; // nanoseconds / Hz
    }
    static int defaultMaxFps()
    {
        return 60;
    }
    static uint defaultRefreshRate()
    {
        return 0;
    }
    static uint defaultVBlankTime()
    {
        return 6000; // 6ms
    }
    static bool defaultGlStrictBinding()
    {
        return true;
    }
    static bool defaultGlStrictBindingFollowsDriver()
    {
        return true;
    }

    base::operation_mode windowing_mode;

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

    /// Deprecated
    void glPlatformInterfaceChanged();
    void windowsBlockCompositingChanged();
    void animationSpeedChanged();
    void animationCurveChanged();

    void configChanged();

private:
    win::focus_policy m_focusPolicy{win::focus_policy::click};
    bool m_nextFocusPrefersMouse{false};
    bool m_clickRaise{false};
    bool m_autoRaise{false};
    int m_autoRaiseInterval{0};
    int m_delayFocusInterval{0};

    bool m_separateScreenFocus{false};

    win::placement m_placement{win::placement::no_placement};
    int m_borderSnapZone{0};
    int m_windowSnapZone{0};
    int m_centerSnapZone{0};
    bool m_snapOnlyWhenOverlapping{false};
    bool m_rollOverDesktops{false};
    win::fsp_level m_focusStealingPreventionLevel{win::fsp_level::none};
    int m_killPingTimeout{0};
    bool m_hideUtilityWindowsForInactive{false};

    CompositingType m_compositingMode{defaultCompositingMode()};
    bool m_useCompositing{defaultUseCompositing()};
    render::x11::hidden_preview m_hiddenPreviews{defaultHiddenPreviews()};
    qint64 m_maxFpsInterval{defaultMaxFpsInterval()};
    // Settings that should be auto-detected
    uint m_refreshRate{defaultRefreshRate()};
    qint64 m_vBlankTime{defaultVBlankTime()};
    bool m_glStrictBinding{defaultGlStrictBinding()};
    bool m_glStrictBindingFollowsDriver{defaultGlStrictBindingFollowsDriver()};
    bool m_windowsBlockCompositing{true};
    render::animation_curve m_animationCurve{render::animation_curve::linear};

    win::win_op OpTitlebarDblClick{defaultOperationTitlebarDblClick()};
    win::win_op opMaxButtonRightClick{defaultOperationMaxButtonRightClick()};
    win::win_op opMaxButtonMiddleClick{defaultOperationMaxButtonMiddleClick()};
    win::win_op opMaxButtonLeftClick{defaultOperationMaxButtonRightClick()};

    // mouse bindings
    win::mouse_cmd CmdActiveTitlebar1{defaultCommandActiveTitlebar1()};
    win::mouse_cmd CmdActiveTitlebar2{defaultCommandActiveTitlebar2()};
    win::mouse_cmd CmdActiveTitlebar3{defaultCommandActiveTitlebar3()};
    win::mouse_cmd CmdInactiveTitlebar1{defaultCommandInactiveTitlebar1()};
    win::mouse_cmd CmdInactiveTitlebar2{defaultCommandInactiveTitlebar2()};
    win::mouse_cmd CmdInactiveTitlebar3{defaultCommandInactiveTitlebar3()};
    win::mouse_wheel_cmd CmdTitlebarWheel{defaultCommandTitlebarWheel()};
    win::mouse_cmd CmdWindow1{defaultCommandWindow1()};
    win::mouse_cmd CmdWindow2{defaultCommandWindow2()};
    win::mouse_cmd CmdWindow3{defaultCommandWindow3()};
    win::mouse_cmd CmdWindowWheel{defaultCommandWindowWheel()};
    win::mouse_cmd CmdAll1{defaultCommandAll1()};
    win::mouse_cmd CmdAll2{defaultCommandAll2()};
    win::mouse_cmd CmdAll3{defaultCommandAll3()};
    win::mouse_wheel_cmd CmdAllWheel{defaultCommandAllWheel()};
    uint CmdAllModKey{defaultKeyCmdAllModKey()};

    bool electric_border_maximize{false};
    bool electric_border_tiling{false};
    float electric_border_corner_ratio{0.};
    bool borderless_maximized_windows{false};
    bool condensed_title{false};

    friend class options;
};

class KWIN_EXPORT options
{
public:
    options(base::operation_mode mode, KSharedConfigPtr config);
    ~options();

    void updateSettings();

    void reloadCompositingSettings(bool force = false);

    /**
     * Performs loading all settings except compositing related.
     */
    void loadConfig();
    /**
     * Performs loading of compositing settings which do not depend on OpenGL.
     */
    bool loadCompositingConfig(bool force);

    /**
     * Returns the animation time factor for desktop effects.
     */
    double animationTimeFactor() const;

    bool get_current_output_follows_mouse() const;
    QStringList modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const;

    static win::win_op windowOperation(const QString& name, bool restricted);
    static win::mouse_cmd mouseCommand(const QString& name, bool restricted);
    static win::mouse_wheel_cmd mouseWheelCommand(const QString& name);

    win::mouse_cmd operationTitlebarMouseWheel(int delta) const
    {
        return wheelToMouseCommand(qobject->CmdTitlebarWheel, delta);
    }
    win::mouse_cmd operationWindowMouseWheel(int delta) const
    {
        return wheelToMouseCommand(qobject->CmdAllWheel, delta);
    }

    std::unique_ptr<options_qobject> qobject;

private:
    void syncFromKcfgc();

    win::mouse_cmd wheelToMouseCommand(win::mouse_wheel_cmd cmd, int delta) const;

    QScopedPointer<Settings> m_settings;
    KConfigWatcher::Ptr m_configWatcher;

    bool current_output_follows_mouse{false};
    QHash<Qt::KeyboardModifier, QStringList> m_modifierOnlyShortcuts;
};

inline std::unique_ptr<options> create_options(operation_mode mode, KSharedConfigPtr config)
{
    auto opts = std::make_unique<base::options>(mode, config);
    opts->loadConfig();
    opts->loadCompositingConfig(false);
    return opts;
}

}
