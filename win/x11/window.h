/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"

#include "win/meta.h"

#include <xcb/sync.h>

#include <memory>
#include <vector>

namespace KWin
{
class GeometryTip;

namespace win::x11
{

constexpr long ClientWinMask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEYMAP_STATE
    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION | // need this, too!
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
    | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

// Window types with control.
constexpr NET::WindowTypes SUPPORTED_MANAGED_WINDOW_TYPES_MASK = NET::NormalMask | NET::DesktopMask
    | NET::DockMask | NET::ToolbarMask | NET::MenuMask
    | NET::DialogMask /*| NET::OverrideMask*/ | NET::TopMenuMask | NET::UtilityMask
    | NET::SplashMask | NET::NotificationMask | NET::OnScreenDisplayMask
    | NET::CriticalNotificationMask;

/**
 * @brief Defines predicate matches on how to search for a window.
 */
enum class predicate_match {
    window,
    wrapper_id,
    frame_id,
    input_id,
};

enum class mapping_state {
    withdrawn, ///< Not handled, as per ICCCM WithdrawnState
    mapped,    ///< The frame is mapped
    unmapped,  ///< The frame is not mapped
    kept,      ///< The frame should be unmapped, but is kept (For compositing)
};

class KWIN_EXPORT window : public Toplevel
{
    Q_OBJECT
public:
    QString iconic_caption;

    struct {
        Xcb::Window client{};
        Xcb::Window wrapper{};
        Xcb::Window frame{};

        // Including decoration.
        Xcb::Window input{};

        // For move-resize operations.
        Xcb::Window grab{};
    } xcb_windows;

    bool m_managed{false};
    bool blocks_compositing{false};
    uint deleting{0};

    bool needs_x_move{false};
    bool move_resize_has_keyboard_grab{false};

    NET::Actions allowed_actions{};

    // Whether the X property was actually set.
    bool activities_defined{false};
    QStringList activity_list;
    int activity_updates_blocked{false};
    bool blocked_activity_updates_require_transients{false};
    bool session_activity_override{false};

    uint user_no_border{0};
    uint app_no_border{0};

    win::maximize_mode max_mode{win::maximize_mode::restore};

    win::shade shade_mode{win::shade::none};
    window* shade_below{nullptr};
    bool shade_geometry_change{false};

    // Forcibly hidden by calling hide()
    uint hidden{0};

    xcb_timestamp_t ping_timestamp{XCB_TIME_CURRENT_TIME};
    xcb_timestamp_t user_time{XCB_TIME_CURRENT_TIME};

    qint64 kill_helper_pid{0};

    struct {
        xcb_sync_counter_t counter{XCB_NONE};
        xcb_sync_int64_t value;
        xcb_sync_alarm_t alarm{XCB_NONE};
        xcb_timestamp_t lastTimestamp;
        QTimer* timeout{nullptr};
        QTimer* failsafeTimeout{nullptr};
        bool isPending{false};
    } sync_request;

    struct {
        QMetaObject::Connection edge_remove;
        QMetaObject::Connection edge_geometry;
    } connections;

    mapping_state mapping{mapping_state::withdrawn};

    Xcb::GeometryHints geometry_hints;
    Xcb::MotifHints motif_hints;

    QTimer* shade_hover_timer{nullptr};
    QTimer* focus_out_timer{nullptr};
    QTimer* ping_timer{nullptr};

    QPoint input_offset;

    int sm_stacking_order{-1};

    Group* in_group{nullptr};

    xcb_colormap_t colormap{XCB_COLORMAP_NONE};

    // TODO(romangg): Make non-static? Or remove geometry tips completely?
    static GeometryTip* geometry_tip;

    explicit window();
    ~window();

    bool isClient() const override;
    xcb_window_t frameId() const override;
    bool providesContextHelp() const override;
    void showContextHelp() override;
    void checkNoBorder() override;
    bool wantsShadowToBeRendered() const override;
    QSize resizeIncrements() const override;
    static void cleanupX11();
    QRect iconGeometry() const override;

    bool setupCompositing(bool add_full_damage) override;
    void finishCompositing(ReleaseReason releaseReason = ReleaseReason::Release) override;
    void setBlockingCompositing(bool block) override;

    void damageNotifyEvent() override;
    void addDamage(QRegion const& damage) override;

    void release_window(bool on_shutdown = false);
    void destroy() override;

    void applyWindowRules() override;
    void updateWindowRules(Rules::Types selection) override;

    win::shade shadeMode() const override;
    bool isShadeable() const override;
    void setShade(win::shade mode) override;
    void toggleShade();

    bool acceptsFocus() const override;
    void updateCaption() override;

    void shade_hover();
    void shade_unhover();
    void cancel_shade_hover_timer();

    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;
    void setClientShown(bool shown) override;

    QRect bufferGeometry() const override;

    QSize clientSize() const override;
    QSize sizeForClientSize(QSize const&,
                            win::size_mode mode = win::size_mode::any,
                            bool noframe = false) const override;

    QSize minSize() const override;
    QSize maxSize() const override;
    QSize basicUnit() const override;

    // TODO: remove
    const Group* group() const override;
    Group* group() override;

    // When another window is created, checks if this window is a child for it.
    void checkTransient(Toplevel* window) override;
    bool groupTransient() const override;
    Toplevel* findModal() override;

    win::maximize_mode maximizeMode() const override;
    void setFullScreen(bool set, bool user = true) override;
    bool userCanSetFullScreen() const override;

    bool noBorder() const override;
    void setNoBorder(bool set) override;
    void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void takeFocus() override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;

    bool performMouseCommand(Options::MouseCommand command, QPoint const& globalPos) override;
    void setShortcutInternal() override;

    bool hasStrut() const override;
    void showOnScreenEdge() override;

    void closeWindow() override;
    bool isCloseable() const override;
    bool isMaximizable() const override;
    bool isMinimizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    void hideClient(bool hide) override;

    void changeMaximize(bool horizontal, bool vertical, bool adjust) override;
    bool doStartMoveResize() override;
    void leaveMoveResize() override;
    bool isWaitingForMoveResizeSync() const override;
    void doResizeSync() override;
    void doPerformMoveResize() override;

    bool belongsToSameApplication(Toplevel const* other,
                                  win::same_client_check checks) const override;
    bool belongsToDesktop() const override;
    void doSetDesktop(int desktop, int was_desk) override;

    QStringList activities() const override;
    void setOnAllActivities(bool set) override;
    void setOnActivities(QStringList newActivitiesList) override;
    void blockActivityUpdates(bool b = true) override;

    xcb_timestamp_t userTime() const override;
    void doSetActive() override;
    void doMinimize() override;

    void resizeWithChecks(QSize const& size,
                          win::force_geometry force = win::force_geometry::no) override;
    void setFrameGeometry(QRect const& rect,
                          win::force_geometry force = win::force_geometry::no) override;

    void updateColorScheme() override;
    void killWindow() override;

    void update_input_shape();

    template<typename T>
    void print(T& stream) const;
    void debug(QDebug& stream) const override;

Q_SIGNALS:
    void clientManaging(KWin::win::x11::window*);
    void clientFullScreenSet(KWin::win::x11::window*, bool, bool);
};

template<typename T>
inline void window::print(T& stream) const
{
    stream << "\'Client:" << xcb_window() << ";WMCLASS:" << resourceClass() << ":" << resourceName()
           << ";Caption:" << win::caption(this) << "\'";
}

}
}

Q_DECLARE_METATYPE(KWin::win::x11::window*)
Q_DECLARE_METATYPE(QList<KWin::win::x11::window*>)
