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

namespace KWin::win::x11
{

constexpr long client_win_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEYMAP_STATE
    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION | // need this, too!
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
    | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

// Window types with control.
constexpr NET::WindowTypes supported_managed_window_types_mask = NET::NormalMask | NET::DesktopMask
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
    constexpr static bool is_toplevel{false};

    explicit window(Workspace& space);
    ~window();

    Workspace& space;

    QString iconic_caption;

    struct {
        // Most outer window that encompasses all other windows.
        Xcb::Window outer{};

        // Window with the same dimensions as client.
        // TODO(romangg): Why do we need this again?
        Xcb::Window wrapper{};

        // The actual client window.
        Xcb::Window client{};

        // Including decoration.
        Xcb::Window input{};

        // For move-resize operations.
        Xcb::Window grab{};
    } xcb_windows;

    bool blocks_compositing{false};
    uint deleting{0};

    // True when X11 Server must be informed about the final location of a move on leaving the move.
    bool move_needs_server_update{false};
    bool move_resize_has_keyboard_grab{false};

    NET::Actions allowed_actions{};

    uint user_no_border{0};
    uint app_no_border{0};

    win::maximize_mode max_mode{win::maximize_mode::restore};
    win::maximize_mode prev_max_mode{win::maximize_mode::restore};

    // Forcibly hidden by calling hide()
    uint hidden{0};

    xcb_timestamp_t ping_timestamp{XCB_TIME_CURRENT_TIME};
    xcb_timestamp_t user_time{XCB_TIME_CURRENT_TIME};

    qint64 kill_helper_pid{0};

    struct {
        xcb_sync_counter_t counter{XCB_NONE};
        xcb_sync_alarm_t alarm{XCB_NONE};

        // The update request number is the serial of our latest configure request.
        int64_t update_request_number{0};
        xcb_timestamp_t timestamp{XCB_NONE};

        int suppressed{0};
    } sync_request;

    struct configure_event {
        int64_t update_request_number{0};

        // Geometry to apply after a resize operation has been completed.
        struct {
            QRect frame;
            // TODO(romangg): instead of client geometry remember deco and extents margins?
            QRect client;
            maximize_mode max_mode{maximize_mode::restore};
            bool fullscreen{false};
        } geometry;
    };
    std::vector<configure_event> pending_configures;

    // The geometry clients are configured with via the sync extension.
    struct {
        QRect frame;
        QRect client;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};
    } synced_geometry;

    bool first_geo_synced{false};

    QTimer* syncless_resize_retarder{nullptr};

    struct {
        QMetaObject::Connection edge_remove;
        QMetaObject::Connection edge_geometry;
    } connections;

    mapping_state mapping{mapping_state::withdrawn};

    Xcb::GeometryHints geometry_hints;
    Xcb::MotifHints motif_hints;

    QTimer* focus_out_timer{nullptr};
    QTimer* ping_timer{nullptr};

    QPoint input_offset;

    int sm_stacking_order{-1};

    x11::group* in_group{nullptr};

    xcb_colormap_t colormap{XCB_COLORMAP_NONE};

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
    void add_scene_window_addon() override;

    void damageNotifyEvent() override;
    void addDamage(QRegion const& damage) override;

    void applyWindowRules() override;
    void updateWindowRules(Rules::Types selection) override;

    bool acceptsFocus() const override;
    void updateCaption() override;

    bool isShown() const override;
    bool isHiddenInternal() const override;

    QSize minSize() const override;
    QSize maxSize() const override;
    QSize basicUnit() const override;

    // TODO: remove
    x11::group const* group() const override;
    x11::group* group() override;

    // When another window is created, checks if this window is a child for it.
    void checkTransient(Toplevel* window) override;
    bool groupTransient() const override;
    Toplevel* findModal() override;

    win::maximize_mode maximizeMode() const override;
    void setFullScreen(bool full, bool user = true) override;
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

    void update_maximized(maximize_mode mode) override;

    bool doStartMoveResize() override;
    void leaveMoveResize() override;
    void doResizeSync() override;
    bool isWaitingForMoveResizeSync() const override;

    bool belongsToSameApplication(Toplevel const* other,
                                  win::same_client_check checks) const override;
    bool belongsToDesktop() const override;
    void doSetDesktop(int desktop, int was_desk) override;

    bool isBlockingCompositing() override;

    xcb_timestamp_t userTime() const override;
    void doSetActive() override;
    void doMinimize() override;

    void setFrameGeometry(QRect const& rect) override;
    void do_set_geometry(QRect const& frame_geo);
    void do_set_maximize_mode(win::maximize_mode mode);
    void do_set_fullscreen(bool full);

    void updateColorScheme() override;
    void killWindow() override;

    void getResourceClass();
    void getWmClientMachine();

    Xcb::Property fetchWmClientLeader() const;
    void readWmClientLeader(Xcb::Property& p);
    void getWmClientLeader();

    /**
     * This function fetches the opaque region from this Toplevel.
     * Will only be called on corresponding property changes and for initialization.
     */
    void getWmOpaqueRegion();
    void getSkipCloseAnimation();

    void detectShape(xcb_window_t id);
    void update_input_shape();

    QByteArray sessionId() const;
    QByteArray wmCommand();

    // TODO(romangg): only required with Xwayland, move it to the child class.
    void clientMessageEvent(xcb_client_message_event_t* e);

    static bool resourceMatch(window const* c1, window const* c2);
    void debug(QDebug& stream) const override;

Q_SIGNALS:
    void client_fullscreen_set(KWin::win::x11::window*, bool, bool);
};

}

Q_DECLARE_METATYPE(KWin::win::x11::window*)
Q_DECLARE_METATYPE(QList<KWin::win::x11::window*>)
