/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"
#include "x11/types.h"

#include "kwin_export.h"
#include "kwinglobals.h"

#include <KConfigWatcher>
#include <QObject>

namespace KWin::win
{

class settings;

class KWIN_EXPORT options_qobject : public QObject
{
    Q_OBJECT

public:
    focus_policy focusPolicy() const
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
        return m_focusPolicy == focus_policy::click || m_focusPolicy == focus_policy::follows_mouse;
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
    fsp_level focusStealingPreventionLevel() const
    {
        return m_focusStealingPreventionLevel;
    }

    win_op operationTitlebarDblClick() const
    {
        return OpTitlebarDblClick;
    }
    win_op operationMaxButtonLeftClick() const
    {
        return opMaxButtonLeftClick;
    }
    win_op operationMaxButtonRightClick() const
    {
        return opMaxButtonRightClick;
    }
    win_op operationMaxButtonMiddleClick() const
    {
        return opMaxButtonMiddleClick;
    }
    win_op operationMaxButtonClick(Qt::MouseButtons button) const;

    mouse_cmd commandActiveTitlebar1() const
    {
        return CmdActiveTitlebar1;
    }
    mouse_cmd commandActiveTitlebar2() const
    {
        return CmdActiveTitlebar2;
    }
    mouse_cmd commandActiveTitlebar3() const
    {
        return CmdActiveTitlebar3;
    }
    mouse_cmd commandInactiveTitlebar1() const
    {
        return CmdInactiveTitlebar1;
    }
    mouse_cmd commandInactiveTitlebar2() const
    {
        return CmdInactiveTitlebar2;
    }
    mouse_cmd commandInactiveTitlebar3() const
    {
        return CmdInactiveTitlebar3;
    }
    mouse_cmd commandWindow1() const
    {
        return CmdWindow1;
    }
    mouse_cmd commandWindow2() const
    {
        return CmdWindow2;
    }
    mouse_cmd commandWindow3() const
    {
        return CmdWindow3;
    }
    mouse_cmd commandWindowWheel() const
    {
        return CmdWindowWheel;
    }
    mouse_cmd commandAll1() const
    {
        return CmdAll1;
    }
    mouse_cmd commandAll2() const
    {
        return CmdAll2;
    }
    mouse_cmd commandAll3() const
    {
        return CmdAll3;
    }
    mouse_wheel_cmd commandAllWheel() const
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

    // setters
    void setFocusPolicy(focus_policy focusPolicy);
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
    void setFocusStealingPreventionLevel(fsp_level lvl);
    void setOperationTitlebarDblClick(win_op op);
    void setOperationMaxButtonLeftClick(win_op op);
    void setOperationMaxButtonRightClick(win_op op);
    void setOperationMaxButtonMiddleClick(win_op op);
    void setCommandActiveTitlebar1(mouse_cmd cmd);
    void setCommandActiveTitlebar2(mouse_cmd cmd);
    void setCommandActiveTitlebar3(mouse_cmd cmd);
    void setCommandInactiveTitlebar1(mouse_cmd cmd);
    void setCommandInactiveTitlebar2(mouse_cmd cmd);
    void setCommandInactiveTitlebar3(mouse_cmd cmd);
    void setCommandWindow1(mouse_cmd cmd);
    void setCommandWindow2(mouse_cmd cmd);
    void setCommandWindow3(mouse_cmd cmd);
    void setCommandWindowWheel(mouse_cmd cmd);
    void setCommandAll1(mouse_cmd cmd);
    void setCommandAll2(mouse_cmd cmd);
    void setCommandAll3(mouse_cmd cmd);
    void setKeyCmdAllModKey(uint keyCmdAllModKey);
    void setCondensedTitle(bool condensedTitle);
    void setElectricBorderMaximize(bool electricBorderMaximize);
    void setElectricBorderTiling(bool electricBorderTiling);
    void setElectricBorderCornerRatio(float electricBorderCornerRatio);
    void setBorderlessMaximizedWindows(bool borderlessMaximizedWindows);
    void setKillPingTimeout(int killPingTimeout);
    void setHideUtilityWindowsForInactive(bool hideUtilityWindowsForInactive);

    // default values
    static win_op defaultOperationTitlebarDblClick()
    {
        return win_op::maximize;
    }
    static win_op defaultOperationMaxButtonLeftClick()
    {
        return win_op::maximize;
    }
    static win_op defaultOperationMaxButtonRightClick()
    {
        return win_op::h_maximize;
    }
    static win_op defaultOperationMaxButtonMiddleClick()
    {
        return win_op::v_maximize;
    }
    static mouse_cmd defaultCommandActiveTitlebar1()
    {
        return mouse_cmd::raise;
    }
    static mouse_cmd defaultCommandActiveTitlebar2()
    {
        return mouse_cmd::nothing;
    }
    static mouse_cmd defaultCommandActiveTitlebar3()
    {
        return mouse_cmd::operations_menu;
    }
    static mouse_cmd defaultCommandInactiveTitlebar1()
    {
        return mouse_cmd::activate_and_raise;
    }
    static mouse_cmd defaultCommandInactiveTitlebar2()
    {
        return mouse_cmd::nothing;
    }
    static mouse_cmd defaultCommandInactiveTitlebar3()
    {
        return mouse_cmd::operations_menu;
    }
    static mouse_cmd defaultCommandWindow1()
    {
        return mouse_cmd::activate_raise_and_pass_click;
    }
    static mouse_cmd defaultCommandWindow2()
    {
        return mouse_cmd::activate_and_pass_click;
    }
    static mouse_cmd defaultCommandWindow3()
    {
        return mouse_cmd::activate_and_pass_click;
    }
    static mouse_cmd defaultCommandWindowWheel()
    {
        return mouse_cmd::nothing;
    }
    static mouse_cmd defaultCommandAll1()
    {
        return mouse_cmd::unrestricted_move;
    }
    static mouse_cmd defaultCommandAll2()
    {
        return mouse_cmd::toggle_raise_and_lower;
    }
    static mouse_cmd defaultCommandAll3()
    {
        return mouse_cmd::unrestricted_resize;
    }
    static mouse_wheel_cmd defaultCommandTitlebarWheel()
    {
        return mouse_wheel_cmd::nothing;
    }
    static mouse_wheel_cmd defaultCommandAllWheel()
    {
        return mouse_wheel_cmd::nothing;
    }
    static uint defaultKeyCmdAllModKey()
    {
        return Qt::Key_Alt;
    }

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

    void configChanged();

private:
    focus_policy m_focusPolicy{focus_policy::click};
    bool m_nextFocusPrefersMouse{false};
    bool m_clickRaise{false};
    bool m_autoRaise{false};
    int m_autoRaiseInterval{0};
    int m_delayFocusInterval{0};

    bool m_separateScreenFocus{false};

    win::placement m_placement{placement::no_placement};
    int m_borderSnapZone{0};
    int m_windowSnapZone{0};
    int m_centerSnapZone{0};
    bool m_snapOnlyWhenOverlapping{false};
    bool m_rollOverDesktops{false};
    fsp_level m_focusStealingPreventionLevel{fsp_level::none};
    int m_killPingTimeout{0};
    bool m_hideUtilityWindowsForInactive{false};

    win_op OpTitlebarDblClick{defaultOperationTitlebarDblClick()};
    win_op opMaxButtonRightClick{defaultOperationMaxButtonRightClick()};
    win_op opMaxButtonMiddleClick{defaultOperationMaxButtonMiddleClick()};
    win_op opMaxButtonLeftClick{defaultOperationMaxButtonRightClick()};

    // mouse bindings
    mouse_cmd CmdActiveTitlebar1{defaultCommandActiveTitlebar1()};
    mouse_cmd CmdActiveTitlebar2{defaultCommandActiveTitlebar2()};
    mouse_cmd CmdActiveTitlebar3{defaultCommandActiveTitlebar3()};
    mouse_cmd CmdInactiveTitlebar1{defaultCommandInactiveTitlebar1()};
    mouse_cmd CmdInactiveTitlebar2{defaultCommandInactiveTitlebar2()};
    mouse_cmd CmdInactiveTitlebar3{defaultCommandInactiveTitlebar3()};
    mouse_wheel_cmd CmdTitlebarWheel{defaultCommandTitlebarWheel()};
    mouse_cmd CmdWindow1{defaultCommandWindow1()};
    mouse_cmd CmdWindow2{defaultCommandWindow2()};
    mouse_cmd CmdWindow3{defaultCommandWindow3()};
    mouse_cmd CmdWindowWheel{defaultCommandWindowWheel()};
    mouse_cmd CmdAll1{defaultCommandAll1()};
    mouse_cmd CmdAll2{defaultCommandAll2()};
    mouse_cmd CmdAll3{defaultCommandAll3()};
    mouse_wheel_cmd CmdAllWheel{defaultCommandAllWheel()};
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
    options(KSharedConfigPtr config);
    ~options();

    void updateSettings();
    void loadConfig();

    bool get_current_output_follows_mouse() const;
    QStringList modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const;

    static win_op windowOperation(const QString& name, bool restricted);
    static mouse_cmd mouseCommand(const QString& name, bool restricted);
    static mouse_wheel_cmd mouseWheelCommand(const QString& name);

    mouse_cmd operationTitlebarMouseWheel(int delta) const
    {
        return wheelToMouseCommand(qobject->CmdTitlebarWheel, delta);
    }
    mouse_cmd operationWindowMouseWheel(int delta) const
    {
        return wheelToMouseCommand(qobject->CmdAllWheel, delta);
    }

    std::unique_ptr<options_qobject> qobject;

private:
    void syncFromKcfgc();

    mouse_cmd wheelToMouseCommand(mouse_wheel_cmd cmd, int delta) const;

    QScopedPointer<settings> m_settings;
    KConfigWatcher::Ptr m_configWatcher;

    bool current_output_follows_mouse{false};
};

}
