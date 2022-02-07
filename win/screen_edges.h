/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <KSharedConfig>

#include <QDateTime>
#include <QObject>
#include <QRect>

#include <memory>
#include <vector>

class QAction;
class QMouseEvent;

namespace KWin
{

namespace input
{
class gesture_recognizer;
class swipe_gesture;
}

class Toplevel;
class Workspace;

namespace win
{

class screen_edger;

class KWIN_EXPORT screen_edge : public QObject
{
    Q_OBJECT
public:
    explicit screen_edge(screen_edger* edger);
    ~screen_edge() override;

    bool isLeft() const;
    bool isTop() const;
    bool isRight() const;
    bool isBottom() const;
    bool isCorner() const;

    bool isScreenEdge() const;
    bool triggersFor(QPoint const& cursorPos) const;
    bool check(QPoint const& cursorPos, QDateTime const& triggerTime, bool forceNoPushBack = false);
    void markAsTriggered(const QPoint& cursorPos, QDateTime const& triggerTime);

    void reserve(QObject* object, const char* slot);
    void reserveTouchCallBack(QAction* action);
    void unreserveTouchCallBack(QAction* action);

    void startApproaching();
    void stopApproaching();
    void setClient(Toplevel* window);
    Toplevel* client() const;

    void set_pointer_action(ElectricBorderAction action);
    void set_touch_action(ElectricBorderAction action);

    bool activatesForPointer() const;
    bool activatesForTouchGesture() const;

    /**
     * The window id of the native window representing the edge.
     * Default implementation returns @c 0, which means no window.
     */
    virtual quint32 window_id() const;
    /**
     * The approach window is a special window to notice when get close to the screen border but
     * not yet triggering the border.
     *
     * The default implementation returns @c 0, which means no window.
     */
    virtual quint32 approachWindow() const;

    QRect geometry;
    ElectricBorder border{ElectricNone};
    std::vector<QAction*> touch_actions;
    int reserved_count{0};
    QHash<QObject*, QByteArray> callbacks;

    bool is_approaching{false};
    QRect approach_geometry;

public Q_SLOTS:
    void reserve();
    void unreserve();
    void unreserve(QObject* object);
    void setBorder(ElectricBorder border);
    void setGeometry(QRect const& geometry);
    void updateApproaching(QPoint const& point);
    void checkBlocking();

Q_SIGNALS:
    void approaching(ElectricBorder border, qreal factor, QRect const& geometry);
    void activatesForTouchGestureChanged();

protected:
    virtual void doGeometryUpdate();
    virtual void doActivate();
    virtual void doDeactivate();
    virtual void doStartApproaching();
    virtual void doStopApproaching();
    virtual void doUpdateBlocking();

    bool is_blocked{false};

private:
    void activate();
    void deactivate();
    bool canActivate(QPoint const& cursorPos, const QDateTime& triggerTime);
    void handle(QPoint const& cursorPos);
    bool handleAction(ElectricBorderAction action);
    bool handlePointerAction()
    {
        return handleAction(pointer_action);
    }
    bool handleTouchAction()
    {
        return handleAction(touch_action);
    }
    bool handleByCallback();
    void handleTouchCallback();
    void switchDesktop(QPoint const& cursorPos);
    void pushCursorBack(QPoint const& cursorPos);

    screen_edger* edger;
    ElectricBorderAction pointer_action{ElectricActionNone};
    ElectricBorderAction touch_action{ElectricActionNone};

    QDateTime last_trigger_time;
    QDateTime last_reset_time;
    QPoint triggered_point;

    int last_approaching_factor{0};
    bool push_back_is_blocked{false};

    Toplevel* window{nullptr};
    std::unique_ptr<input::swipe_gesture> gesture;
};

/**
 * @short Class for controlling screen edges.
 *
 * The screen edge functionality is split into three parts:
 * @li This manager class screen_edger
 * @li abstract class @ref Edge
 * @li specific implementation of @ref Edge, e.g. WindowBasedEdge
 *
 * The screen_edger creates an @ref Edge for each screen edge which is also an edge in the
 * combination of all screens. E.g. if there are two screens, no Edge is created between the
 * screens, but at all other edges even if the screens have a different dimension.
 *
 * In addition at each corner of the overall display geometry an one-pixel large @ref Edge is
 * created. No matter how many screens there are, there will only be exactly four of these corner
 * edges. This is motivated by Fitts's Law which show that it's easy to trigger such a corner, but
 * it would be very difficult to trigger a corner between two screens (one pixel target not visually
 * outlined).
 *
 * Screen edges are used for one of the following functionality:
 * @li switch virtual desktop (see property @ref desktopSwitching)
 * @li switch virtual desktop when moving a window (see property @ref desktopSwitchingMovingClients)
 * @li trigger a pre-defined action (see properties @ref actionTop and similar)
 * @li trigger an externally configured action (e.g. Effect, Script, see @ref reserve, @ref
 * unreserve)
 *
 * An @ref Edge is only active if there is at least one of the possible actions "reserved" for this
 * edge. The idea is to not block the screen edge if nothing could be triggered there, so that the
 * user e.g. can configure nothing on the top edge, which tends to interfere with full screen apps
 * having a hidden panel there. On X11 (currently only supported backend) the @ref Edge is
 * represented by a WindowBasedEdge which creates an input only window for the geometry and
 * reacts on enter notify events. If the edge gets reserved for the first time a window is created
 * and mapped, once the edge gets unreserved again, the window gets destroyed.
 *
 * When the mouse enters one of the screen edges the following values are used to determine whether
 * the action should be triggered or the cursor be pushed back
 * @li Time difference between two entering events is not larger than a certain threshold
 * @li Time difference between two entering events is larger than @ref time_threshold
 * @li Time difference between two activations is larger than @ref reactivate_threshold
 * @li Distance between two enter events is not larger than a defined pixel distance
 * These checks are performed in @ref Edge
 *
 * @todo change way how Effects/Scripts can reserve an edge and are notified.
 */
class KWIN_EXPORT screen_edger : public QObject
{
    Q_OBJECT
public:
    screen_edger(Workspace& space);
    ~screen_edger() override;

    /**
     * Initialize the screen edges.
     * @internal
     */
    void init();

    /**
     * Check, if a screen edge is entered and trigger the appropriate action
     * if one is enabled for the current region and the timeout is satisfied
     * @param pos the position of the mouse pointer
     * @param now the time when the function is called
     * @param forceNoPushBack needs to be called to workaround some DnD clients, don't use unless
     * you want to chek on a DnD event
     */
    void check(QPoint const& pos, QDateTime const& now, bool forceNoPushBack = false);

    /**
     * Mark the specified screen edge as reserved. This method is provided for external activation
     * like effects and scripts. When the effect/script does no longer need the edge it is supposed
     * to call @ref unreserve.
     * @param border the screen edge to mark as reserved
     * @param object The object on which the @p callback needs to be invoked
     * @param callback The method name to be invoked - uses QMetaObject::invokeMethod
     * @see unreserve
     * @todo: add pointer to script/effect
     */
    void reserve(ElectricBorder border, QObject* object, char const* callback);
    /**
     * Mark the specified screen edge as unreserved. This method is provided for external activation
     * like effects and scripts. This method is only allowed to be called if @ref reserve had been
     * called before for the same @p border. An unbalanced calling of reserve/unreserve leads to the
     * edge never being active or never being able to deactivate again.
     * @param border the screen edge to mark as unreserved
     * @param object the object on which the callback had been invoked
     * @see reserve
     * @todo: add pointer to script/effect
     */
    void unreserve(ElectricBorder border, QObject* object);
    /**
     * Reserves an edge for the @p client. The idea behind this is to show the @p client if the
     * screen edge which the @p client borders gets triggered.
     *
     * When first called it is tried to create an Edge for the client. This is only done if the
     * client borders with a screen edge specified by @p border. If the client doesn't border the
     * screen edge, no Edge gets created and the client is shown again. Otherwise there would not
     * be a possibility to show the client again.
     *
     * On subsequent calls for the client no new Edge is created, but the existing one gets reused
     * and if the client is already hidden, the Edge gets reserved.
     *
     * Once the Edge for the client triggers, the client gets shown again and the Edge unreserved.
     * The idea is that the Edge can only get activated if the client is currently hidden.
     *
     * To make sure that the client can always be shown again the implementation also starts to
     * track geometry changes and shows the Client again. The same for screen geometry changes.
     *
     * The Edge gets automatically destroyed if the client gets released.
     * @param client The Client for which an Edge should be reserved
     * @param border The border which the client wants to use, only proper borders are supported (no
     * corners)
     */
    void reserve(Toplevel* window, ElectricBorder border);

    /**
     * Mark the specified screen edge as reserved for touch gestures. This method is provided for
     * external activation like effects and scripts.
     * When the effect/script does no longer need the edge it is supposed
     * to call @ref unreserveTouch.
     * @param border the screen edge to mark as reserved
     * @param action The action which gets triggered
     * @see unreserveTouch
     * @since 5.10
     */
    void reserveTouch(ElectricBorder border, QAction* action);
    /**
     * Unreserves the specified @p border from activating the @p action for touch gestures.
     * @see reserveTouch
     * @since 5.10
     */
    void unreserveTouch(ElectricBorder border, QAction* action);

    /**
     * Reserve desktop switching for screen edges, if @p isToReserve is @c true. Unreserve
     * otherwise.
     * @param isToReserve indicated whether desktop switching should be reserved or unreseved
     * @param o Qt orientations
     */
    void reserveDesktopSwitching(bool isToReserve, Qt::Orientations o);
    /**
     * Raise electric border windows to the real top of the screen. We only need
     * to do this if an effect input window is active.
     */
    void ensureOnTop();
    bool isEntered(QMouseEvent* event);

    /**
     * Returns a std::vector of all existing screen edge windows
     * @return all existing screen edge windows in a std::vector
     */
    std::vector<xcb_window_t> windows() const;

    bool handleDndNotify(xcb_window_t window, QPoint const& point);
    bool handleEnterNotifiy(xcb_window_t window, QPoint const& point, QDateTime const& timestamp);

    std::unique_ptr<input::gesture_recognizer> gesture_recognizer;
    KSharedConfig::Ptr config;
    Workspace& space;

    /// The (dpi dependent) length, reserved for the active corners of each edge - 1/3"
    int corner_offset;
    QSize cursor_push_back_distance;

    struct {
        ElectricBorderAction top_left{ElectricActionNone};
        ElectricBorderAction top{ElectricActionNone};
        ElectricBorderAction top_right{ElectricActionNone};
        ElectricBorderAction right{ElectricActionNone};
        ElectricBorderAction bottom_right{ElectricActionNone};
        ElectricBorderAction bottom{ElectricActionNone};
        ElectricBorderAction bottom_left{ElectricActionNone};
        ElectricBorderAction left{ElectricActionNone};
    } actions;

    struct {
        bool always{false};
        bool when_moving_client{false};
    } desktop_switching;

    /// Minimum time between the push back of the cursor and the activation by re-entering the edge.
    int time_threshold{0};

    /// Minimum time between triggers
    int reactivate_threshold{0};

public Q_SLOTS:
    void reconfigure();
    /**
     * Updates the layout of virtual desktops and adjust the reserved borders in case of
     * virtual desktop switching on edges.
     */
    void updateLayout();
    /**
     * Recreates all edges e.g. after the screen size changes.
     */
    void recreateEdges();

Q_SIGNALS:
    /**
     * Signal emitted during approaching of mouse towards @p border. The @p factor indicates how
     * far away the mouse is from the approaching area. The values are clamped into [0.0,1.0] with
     * @c 0.0 meaning far away from the border, @c 1.0 in trigger distance.
     */
    void approaching(ElectricBorder border, qreal factor, QRect const& geometry);
    void checkBlocking();

private:
    enum {
        ElectricDisabled = 0,
        ElectricMoveOnly = 1,
        ElectricAlways = 2,
    };

    void setDesktopSwitching(bool enable);

    void createHorizontalEdge(ElectricBorder border, QRect const& screen, QRect const& fullArea);
    void createVerticalEdge(ElectricBorder border, QRect const& screen, QRect const& fullArea);
    screen_edge* createEdge(ElectricBorder border,
                            int x,
                            int y,
                            int width,
                            int height,
                            bool createAction = true);

    void setActionForBorder(ElectricBorder border,
                            ElectricBorderAction* oldValue,
                            ElectricBorderAction newValue);
    void setActionForTouchBorder(ElectricBorder border, ElectricBorderAction newValue);
    ElectricBorderAction actionForEdge(screen_edge* edge) const;
    ElectricBorderAction actionForTouchEdge(screen_edge* edge) const;
    void createEdgeForClient(Toplevel* window, ElectricBorder border);
    void deleteEdgeForClient(Toplevel* window);

    Qt::Orientations virtual_desktop_layout{};
    std::vector<screen_edge*> edges;

    QMap<ElectricBorder, ElectricBorderAction> touch_actions;
};

/**********************************************************
 * Inlines screen_edge
 *********************************************************/

inline bool screen_edge::isBottom() const
{
    return border == ElectricBottom || border == ElectricBottomLeft
        || border == ElectricBottomRight;
}

inline bool screen_edge::isLeft() const
{
    return border == ElectricLeft || border == ElectricTopLeft || border == ElectricBottomLeft;
}

inline bool screen_edge::isRight() const
{
    return border == ElectricRight || border == ElectricTopRight || border == ElectricBottomRight;
}

inline bool screen_edge::isTop() const
{
    return border == ElectricTop || border == ElectricTopLeft || border == ElectricTopRight;
}

inline bool screen_edge::isCorner() const
{
    return border == ElectricTopLeft || border == ElectricTopRight || border == ElectricBottomRight
        || border == ElectricBottomLeft;
}

inline bool screen_edge::isScreenEdge() const
{
    return border == ElectricLeft || border == ElectricRight || border == ElectricTop
        || border == ElectricBottom;
}

inline Toplevel* screen_edge::client() const
{
    return window;
}

}
}
