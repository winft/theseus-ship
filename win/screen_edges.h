/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "move.h"
#include "singleton_interface.h"
#include "virtual_desktops.h"

#include "base/x11/xcb/helpers.h"
#include "input/gestures.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QAbstractEventDispatcher>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QObject>
#include <QRect>
#include <memory>
#include <vector>

namespace KWin::win
{

KWIN_EXPORT void lock_screen_saver_via_dbus();

class KWIN_EXPORT screen_edge_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void approaching(ElectricBorder border, qreal factor, QRect const& geometry);
    void activatesForTouchGestureChanged();
};

template<typename Edger>
class screen_edge
{
public:
    Edger* edger;
    using window_t = typename std::remove_reference_t<decltype(edger->space)>::window_t;

    explicit screen_edge(Edger* edger)
        : edger(edger)
        , qobject{std::make_unique<screen_edge_qobject>()}
        , gesture{std::make_unique<input::swipe_gesture>()}
    {
        gesture->setMinimumFingerCount(1);
        gesture->setMaximumFingerCount(1);

        QObject::connect(
            gesture.get(),
            &input::gesture::triggered,
            qobject.get(),
            [this] {
                stopApproaching();
                if (window) {
                    std::visit(overload{[&](auto&& win) { win->showOnScreenEdge(); }}, *window);
                    unreserve();
                    return;
                }
                handleTouchAction();
                handleTouchCallback();
            },
            Qt::QueuedConnection);

        QObject::connect(gesture.get(), &input::swipe_gesture::started, qobject.get(), [this] {
            startApproaching();
        });
        QObject::connect(gesture.get(), &input::swipe_gesture::cancelled, qobject.get(), [this] {
            stopApproaching();
        });
        QObject::connect(
            gesture.get(), &input::swipe_gesture::progress, qobject.get(), [this](qreal progress) {
                int factor = progress * 256.0f;
                if (last_approaching_factor != factor) {
                    last_approaching_factor = factor;
                    Q_EMIT qobject->approaching(
                        border, last_approaching_factor / 256.0f, approach_geometry);
                }
            });

        QObject::connect(
            qobject.get(),
            &screen_edge_qobject::activatesForTouchGestureChanged,
            qobject.get(),
            [this] {
                if (reserved_count > 0) {
                    if (activatesForTouchGesture()) {
                        this->edger->gesture_recognizer->registerGesture(gesture.get());
                    } else {
                        this->edger->gesture_recognizer->unregisterGesture(gesture.get());
                    }
                }
            });
    }

    virtual ~screen_edge() = default;

    bool isLeft() const
    {
        return border == ElectricLeft || border == ElectricTopLeft || border == ElectricBottomLeft;
    }

    bool isTop() const
    {
        return border == ElectricTop || border == ElectricTopLeft || border == ElectricTopRight;
    }

    bool isRight() const
    {
        return border == ElectricRight || border == ElectricTopRight
            || border == ElectricBottomRight;
    }

    bool isBottom() const
    {
        return border == ElectricBottom || border == ElectricBottomLeft
            || border == ElectricBottomRight;
    }

    bool isCorner() const
    {
        return border == ElectricTopLeft || border == ElectricTopRight
            || border == ElectricBottomRight || border == ElectricBottomLeft;
    }

    bool isScreenEdge() const
    {
        return border == ElectricLeft || border == ElectricRight || border == ElectricTop
            || border == ElectricBottom;
    }

    bool triggersFor(QPoint const& cursorPos) const
    {
        if (is_blocked) {
            return false;
        }
        if (!activatesForPointer()) {
            return false;
        }
        if (!geometry.contains(cursorPos)) {
            return false;
        }
        if (isLeft() && cursorPos.x() != geometry.x()) {
            return false;
        }
        if (isRight() && cursorPos.x() != (geometry.x() + geometry.width() - 1)) {
            return false;
        }
        if (isTop() && cursorPos.y() != geometry.y()) {
            return false;
        }
        if (isBottom() && cursorPos.y() != (geometry.y() + geometry.height() - 1)) {
            return false;
        }
        return true;
    }

    bool check(QPoint const& cursorPos, QDateTime const& triggerTime, bool forceNoPushBack = false)
    {
        if (!triggersFor(cursorPos)) {
            return false;
        }
        if (last_trigger_time.isValid()
            && last_trigger_time.msecsTo(triggerTime)
                < edger->reactivate_threshold - edger->time_threshold) {
            // Still in cooldown. reset the time, so the user has to actually keep the mouse still
            // for this long to retrigger
            last_trigger_time = triggerTime;

            return false;
        }

        // no pushback so we have to activate at once
        bool directActivate
            = forceNoPushBack || edger->cursor_push_back_distance.isNull() || window;
        if (directActivate || canActivate(cursorPos, triggerTime)) {
            markAsTriggered(cursorPos, triggerTime);
            handle(cursorPos);
            return true;
        } else {
            pushCursorBack(cursorPos);
            triggered_point = cursorPos;
        }

        return false;
    }

    void markAsTriggered(const QPoint& cursorPos, QDateTime const& triggerTime)
    {
        last_trigger_time = triggerTime;

        // invalidate
        last_reset_time = QDateTime();
        triggered_point = cursorPos;
    }

    void reserve()
    {
        reserved_count++;
        if (reserved_count == 1) {
            // got activated
            activate();
        }
    }

    void unreserve()
    {
        reserved_count--;
        if (reserved_count == 0) {
            // got deactivated
            stopApproaching();
            deactivate();
        }
    }

    uint32_t reserve_callback(std::function<bool(ElectricBorder)> slot)
    {
        replace_callback(++edger->callback_id, std::move(slot));
        return edger->callback_id;
    }

    void replace_callback(uint32_t id, std::function<bool(ElectricBorder)> slot)
    {
        callbacks.insert(id, std::move(slot));
        reserve();
    }

    void unreserve_callback(uint32_t id)
    {
        if (callbacks.remove(id) > 0) {
            unreserve();
        }
    }

    void reserveTouchCallBack(QAction* action)
    {
        if (contains(touch_actions, action)) {
            return;
        }
        QObject::connect(action, &QAction::destroyed, qobject.get(), [this, action] {
            unreserveTouchCallBack(action);
        });
        touch_actions.push_back(action);
        reserve();
    }

    void unreserveTouchCallBack(QAction* action)
    {
        auto it = std::find_if(touch_actions.begin(), touch_actions.end(), [action](QAction* a) {
            return a == action;
        });
        if (it != touch_actions.end()) {
            touch_actions.erase(it);
            unreserve();
        }
    }

    void setBorder(ElectricBorder border)
    {
        this->border = border;
        switch (border) {
        case ElectricTop:
            gesture->setDirection(input::swipe_gesture::Direction::Down);
            break;
        case ElectricRight:
            gesture->setDirection(input::swipe_gesture::Direction::Left);
            break;
        case ElectricBottom:
            gesture->setDirection(input::swipe_gesture::Direction::Up);
            break;
        case ElectricLeft:
            gesture->setDirection(input::swipe_gesture::Direction::Right);
            break;
        default:
            break;
        }
    }

    void setGeometry(QRect const& geometry)
    {
        if (this->geometry == geometry) {
            return;
        }
        this->geometry = geometry;

        int x = geometry.x();
        int y = geometry.y();
        int width = geometry.width();
        int height = geometry.height();

        int const offset = edger->corner_offset;

        if (isCorner()) {
            if (isRight()) {
                x = x + width - offset;
            }
            if (isBottom()) {
                y = y + height - offset;
            }
            width = offset;
            height = offset;
        } else {
            if (isLeft()) {
                y += offset;
                width = offset;
                height = height - offset * 2;
            } else if (isRight()) {
                x = x + width - offset;
                y += offset;
                width = offset;
                height = height - offset * 2;
            } else if (isTop()) {
                x += offset;
                width = width - offset * 2;
                height = offset;
            } else if (isBottom()) {
                x += offset;
                y = y + height - offset;
                width = width - offset * 2;
                height = offset;
            }
        }

        approach_geometry = QRect(x, y, width, height);
        doGeometryUpdate();

        if (isScreenEdge()) {
            auto output
                = base::get_nearest_output(edger->space.base.outputs, this->geometry.center());
            assert(output);
            gesture->setStartGeometry(this->geometry);
            gesture->setMinimumDelta(QSizeF(MINIMUM_DELTA, MINIMUM_DELTA) / output->scale());
        }
    }

    void updateApproaching(QPoint const& point)
    {
        if (approach_geometry.contains(point)) {
            int factor = 0;
            const int edgeDistance = edger->corner_offset;
            auto cornerDistance = [=](QPoint const& corner) {
                return qMax(qAbs(corner.x() - point.x()), qAbs(corner.y() - point.y()));
            };
            switch (border) {
            case ElectricTopLeft:
                factor = (cornerDistance(approach_geometry.topLeft()) << 8) / edgeDistance;
                break;
            case ElectricTopRight:
                factor = (cornerDistance(approach_geometry.topRight()) << 8) / edgeDistance;
                break;
            case ElectricBottomRight:
                factor = (cornerDistance(approach_geometry.bottomRight()) << 8) / edgeDistance;
                break;
            case ElectricBottomLeft:
                factor = (cornerDistance(approach_geometry.bottomLeft()) << 8) / edgeDistance;
                break;
            case ElectricTop:
                factor = (qAbs(point.y() - approach_geometry.y()) << 8) / edgeDistance;
                break;
            case ElectricRight:
                factor = (qAbs(point.x() - approach_geometry.right()) << 8) / edgeDistance;
                break;
            case ElectricBottom:
                factor = (qAbs(point.y() - approach_geometry.bottom()) << 8) / edgeDistance;
                break;
            case ElectricLeft:
                factor = (qAbs(point.x() - approach_geometry.x()) << 8) / edgeDistance;
                break;
            default:
                break;
            }
            factor = 256 - factor;
            if (last_approaching_factor != factor) {
                last_approaching_factor = factor;
                Q_EMIT qobject->approaching(
                    border, last_approaching_factor / 256.0f, approach_geometry);
            }
        } else {
            stopApproaching();
        }
    }

    void checkBlocking()
    {
        auto window = edger->space.stacking.active;

        auto newValue = !edger->remainActiveOnFullscreen() && window
            && !(edger->space.base.render->compositor->effects
                 && edger->space.base.render->compositor->effects->hasActiveFullScreenEffect());
        if (newValue) {
            newValue = std::visit(overload{[&](auto&& win) {
                                      return win->control->fullscreen
                                          && win->geo.frame.contains(geometry.center());
                                  }},
                                  *window);
        }

        if (newValue == is_blocked) {
            return;
        }

        bool const wasTouch = activatesForTouchGesture();
        is_blocked = newValue;

        if (is_blocked && is_approaching) {
            stopApproaching();
        }
        if (wasTouch != activatesForTouchGesture()) {
            Q_EMIT qobject->activatesForTouchGestureChanged();
        }
        doUpdateBlocking();
    }

    void startApproaching()
    {
        if (is_approaching) {
            return;
        }
        is_approaching = true;
        doStartApproaching();
        last_approaching_factor = 0;
        Q_EMIT qobject->approaching(border, 0.0, approach_geometry);
    }

    void stopApproaching()
    {
        if (!is_approaching) {
            return;
        }
        is_approaching = false;
        doStopApproaching();
        last_approaching_factor = 0;
        Q_EMIT qobject->approaching(border, 0.0, approach_geometry);
    }

    template<typename Win>
    void setClient(Win* window)
    {
        const bool wasTouch = activatesForTouchGesture();
        this->window = window;
        if (wasTouch != activatesForTouchGesture()) {
            Q_EMIT qobject->activatesForTouchGestureChanged();
        }
    }

    std::optional<window_t> client() const
    {
        return window;
    }

    void set_pointer_action(ElectricBorderAction action)
    {
        pointer_action = action;
    }

    void set_touch_action(ElectricBorderAction action)
    {
        const bool wasTouch = activatesForTouchGesture();
        touch_action = action;
        if (wasTouch != activatesForTouchGesture()) {
            Q_EMIT qobject->activatesForTouchGestureChanged();
        }
    }

    bool activatesForPointer() const
    {
        if (window) {
            return true;
        }
        if (edger->desktop_switching.always) {
            return true;
        }
        if (edger->desktop_switching.when_moving_client) {
            auto c = edger->space.move_resize_window;
            if (c && std::visit(overload{[&](auto&& win) { return !win::is_resize(win); }}, *c)) {
                return true;
            }
        }
        if (!callbacks.isEmpty()) {
            return true;
        }
        if (pointer_action != ElectricActionNone) {
            return true;
        }
        return false;
    }

    bool activatesForTouchGesture() const
    {
        if (!isScreenEdge()) {
            return false;
        }
        if (is_blocked) {
            return false;
        }
        if (window) {
            return true;
        }
        if (touch_action != ElectricActionNone) {
            return true;
        }
        if (!touch_actions.empty()) {
            return true;
        }
        return false;
    }

    /**
     * The window id of the native window representing the edge.
     * Default implementation returns @c 0, which means no window.
     */
    virtual quint32 window_id() const
    {
        return 0;
    }

    /**
     * The approach window is a special window to notice when get close to the screen border but
     * not yet triggering the border.
     *
     * The default implementation returns @c 0, which means no window.
     */
    virtual quint32 approachWindow() const
    {
        return 0;
    }

    std::unique_ptr<screen_edge_qobject> qobject;

    QRect geometry;
    ElectricBorder border{ElectricNone};
    std::vector<QAction*> touch_actions;
    int reserved_count{0};
    QHash<uint32_t, std::function<bool(ElectricBorder)>> callbacks;

    bool is_blocked{false};
    bool is_approaching{false};
    QRect approach_geometry;

protected:
    virtual void doGeometryUpdate()
    {
    }

    virtual void doActivate()
    {
    }

    virtual void doDeactivate()
    {
    }

    virtual void doStartApproaching()
    {
    }

    virtual void doStopApproaching()
    {
    }

    virtual void doUpdateBlocking()
    {
    }

private:
    // Mouse should not move more than this many pixels
    static int const DISTANCE_RESET = 30;

    // How far the user needs to swipe before triggering an action.
    static const int MINIMUM_DELTA = 44;

    void activate()
    {
        if (activatesForTouchGesture()) {
            edger->gesture_recognizer->registerGesture(gesture.get());
        }
        doActivate();
    }

    void deactivate()
    {
        edger->gesture_recognizer->unregisterGesture(gesture.get());
        doDeactivate();
    }

    bool canActivate(QPoint const& cursorPos, const QDateTime& triggerTime)
    {
        // we check whether either the timer has explicitly been invalidated (successful trigger) or
        // is bigger than the reactivation threshold (activation "aborted", usually due to moving
        // away the cursor from the corner after successful activation) either condition means that
        // "this is the first event in a new attempt"
        if (!last_reset_time.isValid()
            || last_reset_time.msecsTo(triggerTime) > edger->reactivate_threshold) {
            last_reset_time = triggerTime;
            return false;
        }

        if (last_trigger_time.isValid()
            && last_trigger_time.msecsTo(triggerTime)
                < edger->reactivate_threshold - edger->time_threshold) {
            return false;
        }

        if (last_reset_time.msecsTo(triggerTime) < edger->time_threshold) {
            return false;
        }

        // does the check on position make any sense at all?
        if ((cursorPos - triggered_point).manhattanLength() > DISTANCE_RESET) {
            return false;
        }
        return true;
    }

    void handle(QPoint const& cursorPos)
    {
        auto& movingClient = edger->space.move_resize_window;

        if ((edger->desktop_switching.when_moving_client && movingClient
             && std::visit(overload{[&](auto&& win) { return !win::is_resize(win); }},
                           *movingClient))
            || (edger->desktop_switching.always && isScreenEdge())) {
            // always switch desktops in case:
            // moving a Client and option for switch on client move is enabled
            // or switch on screen edge is enabled
            switchDesktop(cursorPos);
            return;
        }

        if (movingClient) {
            // if we are moving a window we don't want to trigger the actions. This just results in
            // problems, e.g. Desktop Grid activated or screen locker activated which just cannot
            // work as we hold a grab.
            return;
        }

        if (window) {
            pushCursorBack(cursorPos);
            std::visit(overload{[&](auto&& win) { win->showOnScreenEdge(); }}, *window);
            unreserve();
            return;
        }

        if (handlePointerAction() || handleByCallback()) {
            pushCursorBack(cursorPos);
            return;
        }

        if (edger->desktop_switching.always && isCorner()) {
            // try again desktop switching for the corner
            switchDesktop(cursorPos);
        }
    }

    bool handleAction(ElectricBorderAction action)
    {
        switch (action) {
        case ElectricActionShowDesktop: {
            set_showing_desktop(edger->space, !edger->space.showing_desktop);
            return true;
        }
        case ElectricActionLockScreen: { // Lock the screen
            lock_screen_saver_via_dbus();
            return true;
        }
        case ElectricActionKRunner: { // open krunner
            QDBusConnection::sessionBus().asyncCall(
                QDBusMessage::createMethodCall(QStringLiteral("org.kde.krunner"),
                                               QStringLiteral("/App"),
                                               QStringLiteral("org.kde.krunner.App"),
                                               QStringLiteral("display")));
            return true;
        }
        case ElectricActionApplicationLauncher: {
            QDBusConnection::sessionBus().asyncCall(
                QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                               QStringLiteral("/PlasmaShell"),
                                               QStringLiteral("org.kde.PlasmaShell"),
                                               QStringLiteral("activateLauncherMenu")));
            return true;
        }
        default:
            return false;
        }
    }

    bool handlePointerAction()
    {
        return handleAction(pointer_action);
    }
    bool handleTouchAction()
    {
        return handleAction(touch_action);
    }
    bool handleByCallback()
    {
        if (callbacks.isEmpty()) {
            return false;
        }

        for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
            if (it.value()(border)) {
                return true;
            }
        }

        return false;
    }

    void handleTouchCallback()
    {
        if (touch_actions.empty()) {
            return;
        }
        touch_actions.front()->trigger();
    }

    void switchDesktop(QPoint const& cursorPos)
    {
        QPoint pos(cursorPos);
        auto& vds = edger->space.virtual_desktop_manager;
        auto const oldDesktop = vds->currentDesktop();
        auto desktop = oldDesktop;
        int const OFFSET = 2;

        if (isLeft()) {
            auto const interimDesktop = desktop;
            desktop = vds->toLeft(desktop, vds->isNavigationWrappingAround());
            if (desktop != interimDesktop)
                pos.setX(edger->space.base.topology.size.width() - 1 - OFFSET);
        } else if (isRight()) {
            auto const interimDesktop = desktop;
            desktop = vds->toRight(desktop, vds->isNavigationWrappingAround());
            if (desktop != interimDesktop)
                pos.setX(OFFSET);
        }

        if (isTop()) {
            auto const interimDesktop = desktop;
            desktop = vds->above(desktop, vds->isNavigationWrappingAround());
            if (desktop != interimDesktop)
                pos.setY(edger->space.base.topology.size.height() - 1 - OFFSET);
        } else if (isBottom()) {
            auto const interimDesktop = desktop;
            desktop = vds->below(desktop, vds->isNavigationWrappingAround());
            if (desktop != interimDesktop)
                pos.setY(OFFSET);
        }

        if (auto& mov_res = edger->space.move_resize_window) {
            QVector<virtual_desktop*> desktops{desktop};
            if (std::visit(overload{[&](auto&& win) {
                               return win->control->rules.checkDesktops(
                                   *edger->space.virtual_desktop_manager, desktops);
                           }},
                           *mov_res)
                != desktops) {
                // User tries to move a client to another desktop where it is ruleforced to not be.
                return;
            }
        }

        vds->setCurrent(desktop);

        if (vds->currentDesktop() != oldDesktop) {
            push_back_is_blocked = true;
            edger->space.input->cursor->set_pos(pos);

            QSharedPointer<QMetaObject::Connection> me(new QMetaObject::Connection);
            *me = QObject::connect(
                QCoreApplication::eventDispatcher(),
                &QAbstractEventDispatcher::aboutToBlock,
                qobject.get(),
                [this, me]() {
                    QObject::disconnect(*me);
                    const_cast<QSharedPointer<QMetaObject::Connection>*>(&me)->reset(nullptr);
                    push_back_is_blocked = false;
                });
        }
    }

    void pushCursorBack(QPoint const& cursorPos)
    {
        if (push_back_is_blocked) {
            return;
        }

        int x = cursorPos.x();
        int y = cursorPos.y();

        auto const& distance = edger->cursor_push_back_distance;

        if (isLeft()) {
            x += distance.width();
        }
        if (isRight()) {
            x -= distance.width();
        }
        if (isTop()) {
            y += distance.height();
        }
        if (isBottom()) {
            y -= distance.height();
        }

        edger->space.input->cursor->set_pos(x, y);
    }

    ElectricBorderAction pointer_action{ElectricActionNone};
    ElectricBorderAction touch_action{ElectricActionNone};

    QDateTime last_trigger_time;
    QDateTime last_reset_time;
    QPoint triggered_point;

    int last_approaching_factor{0};
    bool push_back_is_blocked{false};

    std::optional<window_t> window;
    std::unique_ptr<input::swipe_gesture> gesture;
};

class KWIN_EXPORT screen_edger_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    /**
     * Signal emitted during approaching of mouse towards @p border. The @p factor indicates how
     * far away the mouse is from the approaching area. The values are clamped into [0.0,1.0] with
     * @c 0.0 meaning far away from the border, @c 1.0 in trigger distance.
     */
    void approaching(ElectricBorder border, qreal factor, QRect const& geometry);
    void checkBlocking();
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
template<typename Space>
class screen_edger
{
public:
    screen_edger(Space& space)
        : qobject{std::make_unique<screen_edger_qobject>()}
        , gesture_recognizer{std::make_unique<input::gesture_recognizer>()}
        , space{space}
        , singleton{
              [this](auto border, auto callback) { return reserve(border, callback); },
              [this](auto border, auto id) { return unreserve(border, id); },
              [this](auto border, auto action) { return reserveTouch(border, action); },
              [this](auto border, auto action) { return unreserveTouch(border, action); },
          }
    {
        singleton_interface::edger = &singleton;
        int const gridUnit = QFontMetrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont))
                                 .boundingRect(QLatin1Char('M'))
                                 .height();
        corner_offset = 4 * gridUnit;

        config = space.base.config.main;

        reconfigure();
        updateLayout();
        recreateEdges();

        QObject::connect(space.base.options->qobject.get(),
                         &base::options_qobject::configChanged,
                         qobject.get(),
                         [this] { reconfigure(); });
        QObject::connect(space.virtual_desktop_manager->qobject.get(),
                         &virtual_desktop_manager_qobject::layoutChanged,
                         qobject.get(),
                         [this] { updateLayout(); });

        QObject::connect(space.qobject.get(),
                         &Space::qobject_t::clientActivated,
                         qobject.get(),
                         &screen_edger_qobject::checkBlocking);
        QObject::connect(space.qobject.get(),
                         &Space::qobject_t::clientRemoved,
                         qobject.get(),
                         [this](auto win_id) {
                             auto win = this->space.windows_map.at(win_id);
                             deleteEdgeForClient(win);
                         });
    }

    ~screen_edger()
    {
        singleton_interface::edger = nullptr;
    }

    /**
     * Check, if a screen edge is entered and trigger the appropriate action
     * if one is enabled for the current region and the timeout is satisfied
     * @param pos the position of the mouse pointer
     * @param now the time when the function is called
     * @param forceNoPushBack needs to be called to workaround some DnD clients, don't use unless
     * you want to chek on a DnD event
     */
    void check(QPoint const& pos, QDateTime const& now, bool forceNoPushBack = false)
    {
        bool activatedForClient = false;

        for (auto& edge : edges) {
            if (edge->reserved_count == 0 || edge->is_blocked) {
                continue;
            }
            if (!edge->activatesForPointer()) {
                continue;
            }
            if (edge->approach_geometry.contains(pos)) {
                edge->startApproaching();
            }
            if (edge->client() && activatedForClient) {
                edge->markAsTriggered(pos, now);
                continue;
            }
            if (edge->check(pos, now, forceNoPushBack)) {
                if (edge->client()) {
                    activatedForClient = true;
                }
            }
        }
    }

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
    uint32_t reserve(ElectricBorder border, std::function<bool(ElectricBorder)> callback)
    {
        auto id = ++callback_id;
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->replace_callback(id, callback);
            }
        }
        return id;
    }

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
    void unreserve(ElectricBorder border, uint32_t id)
    {
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->unreserve_callback(id);
            }
        }
    }

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
    template<typename Win>
    void reserve(Win* window, ElectricBorder border)
    {
        using var_win = typename Win::space_t::window_t;

        bool hadBorder = false;
        auto it = edges.begin();

        while (it != edges.end()) {
            if (auto win = (*it)->client(); win && *win == var_win(window)) {
                hadBorder = true;
                it = edges.erase(it);
            } else {
                it++;
            }
        }

        if (border != ElectricNone) {
            createEdgeForClient(window, border);
        } else {
            if (hadBorder) // show again
                window->showOnScreenEdge();
        }
    }

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
    void reserveTouch(ElectricBorder border, QAction* action)
    {
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->reserveTouchCallBack(action);
            }
        }
    }

    /**
     * Unreserves the specified @p border from activating the @p action for touch gestures.
     * @see reserveTouch
     * @since 5.10
     */
    void unreserveTouch(ElectricBorder border, QAction* action)
    {
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->unreserveTouchCallBack(action);
            }
        }
    }

    /**
     * Reserve desktop switching for screen edges, if @p isToReserve is @c true. Unreserve
     * otherwise.
     * @param isToReserve indicated whether desktop switching should be reserved or unreseved
     * @param o Qt orientations
     */
    void reserveDesktopSwitching(bool isToReserve, Qt::Orientations o)
    {
        if (!o)
            return;
        for (auto& edge : edges) {
            if (edge->isCorner()) {
                isToReserve ? edge->reserve() : edge->unreserve();
            } else {
                if ((virtual_desktop_layout & Qt::Horizontal)
                    && (edge->isLeft() || edge->isRight())) {
                    isToReserve ? edge->reserve() : edge->unreserve();
                }
                if ((virtual_desktop_layout & Qt::Vertical)
                    && (edge->isTop() || edge->isBottom())) {
                    isToReserve ? edge->reserve() : edge->unreserve();
                }
            }
        }
    }

    /**
     * Raise electric border windows to the real top of the screen. We only need
     * to do this if an effect input window is active.
     */
    void ensureOnTop()
    {
        base::x11::xcb::restack_windows_with_raise(space.base.x11_data.connection, windows());
    }

    bool isEntered(QMouseEvent* event)
    {
        assert(event->type() == QEvent::MouseMove);

        bool activated = false;
        bool activatedForClient = false;

        for (auto& edge : edges) {
            if (edge->reserved_count == 0 || edge->is_blocked) {
                continue;
            }
            if (!edge->activatesForPointer()) {
                continue;
            }

            if (edge->approach_geometry.contains(event->globalPos())) {
                if (!edge->is_approaching) {
                    edge->startApproaching();
                } else {
                    edge->updateApproaching(event->globalPos());
                }
            } else {
                if (edge->is_approaching) {
                    edge->stopApproaching();
                }
            }

            if (edge->geometry.contains(event->globalPos())) {
                if (edge->check(event->globalPos(),
                                QDateTime::fromMSecsSinceEpoch(event->timestamp(), Qt::UTC))) {
                    if (edge->client()) {
                        activatedForClient = true;
                    }
                }
            }
        }

        if (activatedForClient) {
            for (auto& edge : edges) {
                if (edge->client()) {
                    edge->markAsTriggered(
                        event->globalPos(),
                        QDateTime::fromMSecsSinceEpoch(event->timestamp(), Qt::UTC));
                }
            }
        }

        return activated;
    }

    /**
     * Returns a std::vector of all existing screen edge windows
     * @return all existing screen edge windows in a std::vector
     */
    std::vector<xcb_window_t> windows() const
    {
        std::vector<xcb_window_t> wins;

        for (auto& edge : edges) {
            xcb_window_t w = edge->window_id();
            if (w != XCB_WINDOW_NONE) {
                wins.push_back(w);
            }

            // TODO:  lambda
            w = edge->approachWindow();

            if (w != XCB_WINDOW_NONE) {
                wins.push_back(w);
            }
        }

        return wins;
    }

    bool handleDndNotify(xcb_window_t window, QPoint const& point)
    {
        for (auto& edge : edges) {
            if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
                continue;
            }
            if (edge->reserved_count > 0 && edge->window_id() == window) {
                base::x11::update_time_from_clock(space.base);
                edge->check(
                    point, QDateTime::fromMSecsSinceEpoch(space.base.x11_data.time, Qt::UTC), true);
                return true;
            }
        }
        return false;
    }

    bool handleEnterNotifiy(xcb_window_t window, QPoint const& point, QDateTime const& timestamp)
    {
        bool activated = false;
        bool activatedForClient = false;

        for (auto& edge : edges) {
            if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
                continue;
            }
            if (edge->reserved_count == 0 || edge->is_blocked) {
                continue;
            }
            if (!edge->activatesForPointer()) {
                continue;
            }

            if (edge->window_id() == window) {
                if (edge->check(point, timestamp)) {
                    if (edge->client()) {
                        activatedForClient = true;
                    }
                }
                activated = true;
                break;
            }

            if (edge->approachWindow() == window) {
                edge->startApproaching();
                // TODO: if it's a corner, it should also trigger for other windows
                return true;
            }
        }

        if (activatedForClient) {
            for (auto& edge : edges) {
                if (edge->client()) {
                    edge->markAsTriggered(point, timestamp);
                }
            }
        }

        return activated;
    }

    bool remainActiveOnFullscreen() const
    {
        return m_remainActiveOnFullscreen;
    }

    void reconfigure()
    {
        if (!config) {
            return;
        }

        KConfigGroup screenEdgesConfig = config->group("ScreenEdges");
        setRemainActiveOnFullscreen(screenEdgesConfig.readEntry("RemainActiveOnFullscreen", false));

        // TODO: migrate settings to a group ScreenEdges
        auto windowsConfig = config->group("Windows");

        time_threshold = windowsConfig.readEntry("ElectricBorderDelay", 150);
        reactivate_threshold
            = qMax(time_threshold + 50, windowsConfig.readEntry("ElectricBorderCooldown", 350));

        int desktopSwitching
            = windowsConfig.readEntry("ElectricBorders", static_cast<int>(ElectricDisabled));
        if (desktopSwitching == ElectricDisabled) {
            setDesktopSwitching(false);
            desktop_switching.when_moving_client = false;
        } else if (desktopSwitching == ElectricMoveOnly) {
            setDesktopSwitching(false);
            desktop_switching.when_moving_client = true;
        } else if (desktopSwitching == ElectricAlways) {
            setDesktopSwitching(true);
            desktop_switching.when_moving_client = true;
        }
        const int pushBack = windowsConfig.readEntry("ElectricBorderPushbackPixels", 1);
        cursor_push_back_distance = QSize(pushBack, pushBack);

        auto borderConfig = config->group("ElectricBorders");
        setActionForBorder(ElectricTopLeft,
                           &actions.top_left,
                           electricBorderAction(borderConfig.readEntry("TopLeft", "None")));
        setActionForBorder(
            ElectricTop, &actions.top, electricBorderAction(borderConfig.readEntry("Top", "None")));
        setActionForBorder(ElectricTopRight,
                           &actions.top_right,
                           electricBorderAction(borderConfig.readEntry("TopRight", "None")));
        setActionForBorder(ElectricRight,
                           &actions.right,
                           electricBorderAction(borderConfig.readEntry("Right", "None")));
        setActionForBorder(ElectricBottomRight,
                           &actions.bottom_right,
                           electricBorderAction(borderConfig.readEntry("BottomRight", "None")));
        setActionForBorder(ElectricBottom,
                           &actions.bottom,
                           electricBorderAction(borderConfig.readEntry("Bottom", "None")));
        setActionForBorder(ElectricBottomLeft,
                           &actions.bottom_left,
                           electricBorderAction(borderConfig.readEntry("BottomLeft", "None")));
        setActionForBorder(ElectricLeft,
                           &actions.left,
                           electricBorderAction(borderConfig.readEntry("Left", "None")));

        borderConfig = config->group("TouchEdges");
        setActionForTouchBorder(ElectricTop,
                                electricBorderAction(borderConfig.readEntry("Top", "None")));
        setActionForTouchBorder(ElectricRight,
                                electricBorderAction(borderConfig.readEntry("Right", "None")));
        setActionForTouchBorder(ElectricBottom,
                                electricBorderAction(borderConfig.readEntry("Bottom", "None")));
        setActionForTouchBorder(ElectricLeft,
                                electricBorderAction(borderConfig.readEntry("Left", "None")));
    }

    /// Updates virtual desktops layout, adjusts reserved borders in case of vd switching on edges.
    void updateLayout()
    {
        auto const desktopMatrix = space.virtual_desktop_manager->grid().size();
        Qt::Orientations newLayout = {};
        if (desktopMatrix.width() > 1) {
            newLayout |= Qt::Horizontal;
        }
        if (desktopMatrix.height() > 1) {
            newLayout |= Qt::Vertical;
        }
        if (newLayout == virtual_desktop_layout) {
            return;
        }
        if (desktop_switching.always) {
            reserveDesktopSwitching(false, virtual_desktop_layout);
        }
        virtual_desktop_layout = newLayout;
        if (desktop_switching.always) {
            reserveDesktopSwitching(true, virtual_desktop_layout);
        }
    }

    /// Recreates all edges e.g. after the screen size changes.
    void recreateEdges()
    {
        auto const& outputs = space.base.outputs;

        auto oldEdges = std::move(edges);
        assert(edges.empty());

        auto const fullArea = QRect({}, space.base.topology.size);
        QRegion processedRegion;
        for (auto output : outputs) {
            auto const screen = QRegion(output->geometry()).subtracted(processedRegion);
            processedRegion += screen;

            for (QRect const& screenPart : screen) {
                if (isLeftScreen(screenPart, fullArea)) {
                    // left most screen
                    createVerticalEdge(ElectricLeft, screenPart, fullArea);
                }
                if (isRightScreen(screenPart, fullArea)) {
                    // right most screen
                    createVerticalEdge(ElectricRight, screenPart, fullArea);
                }
                if (isTopScreen(screenPart, fullArea)) {
                    // top most screen
                    createHorizontalEdge(ElectricTop, screenPart, fullArea);
                }
                if (isBottomScreen(screenPart, fullArea)) {
                    // bottom most screen
                    createHorizontalEdge(ElectricBottom, screenPart, fullArea);
                }
            }
        }

        // copy over the effect/script reservations from the old edges
        for (auto& edge : edges) {
            for (auto& oldEdge : oldEdges) {
                if (oldEdge->client()) {
                    // show the client again and don't recreate the edge
                    std::visit(overload{[&](auto&& win) { win->showOnScreenEdge(); }},
                               *oldEdge->client());
                    continue;
                }
                if (oldEdge->border != edge->border) {
                    continue;
                }
                auto const& callbacks = oldEdge->callbacks;
                for (auto callback = callbacks.cbegin(); callback != callbacks.cend(); ++callback) {
                    edge->replace_callback(callback.key(), callback.value());
                }
                const auto touchCallBacks = oldEdge->touch_actions;
                for (auto a : touchCallBacks) {
                    edge->reserveTouchCallBack(a);
                }
            }
        }
    }

    std::unique_ptr<screen_edger_qobject> qobject;

    std::unique_ptr<input::gesture_recognizer> gesture_recognizer;
    KSharedConfig::Ptr config;
    Space& space;

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

    uint32_t callback_id{0};

    std::vector<std::unique_ptr<screen_edge<screen_edger>>> edges;

private:
    enum {
        ElectricDisabled = 0,
        ElectricMoveOnly = 1,
        ElectricAlways = 2,
    };

    void setDesktopSwitching(bool enable)
    {
        if (enable == desktop_switching.always) {
            return;
        }
        desktop_switching.always = enable;
        reserveDesktopSwitching(enable, virtual_desktop_layout);
    }

    // How large the touch target of the area recognizing touch gestures is
    static const int TOUCH_TARGET = 3;

    void createHorizontalEdge(ElectricBorder border, QRect const& screen, QRect const& fullArea)
    {
        if (border != ElectricTop && border != ElectricBottom) {
            return;
        }
        int x = screen.x();
        int width = screen.width();
        if (isLeftScreen(screen, fullArea)) {
            // also left most - adjust only x and width
            x += corner_offset;
            width -= corner_offset;
        }
        if (isRightScreen(screen, fullArea)) {
            // also right most edge
            width -= corner_offset;
        }
        if (width <= corner_offset) {
            // An overlap with another output is near complete. We ignore this border.
            return;
        }
        int const y
            = (border == ElectricTop) ? screen.y() : screen.y() + screen.height() - TOUCH_TARGET;
        edges.push_back(createEdge(border, x, y, width, TOUCH_TARGET));
    }

    void createVerticalEdge(ElectricBorder border, QRect const& screen, QRect const& fullArea)
    {
        if (border != ElectricRight && border != KWin::ElectricLeft) {
            return;
        }
        int y = screen.y();
        int height = screen.height();
        int const x
            = (border == ElectricLeft) ? screen.x() : screen.x() + screen.width() - TOUCH_TARGET;
        if (isTopScreen(screen, fullArea)) {
            // also top most screen
            height -= corner_offset;
            y += corner_offset;
            // create top left/right edge
            ElectricBorder const edge
                = (border == ElectricLeft) ? ElectricTopLeft : ElectricTopRight;
            edges.push_back(createEdge(edge, x, screen.y(), TOUCH_TARGET, TOUCH_TARGET));
        }
        if (isBottomScreen(screen, fullArea)) {
            // also bottom most screen
            height -= corner_offset;
            // create bottom left/right edge
            ElectricBorder const edge
                = (border == ElectricLeft) ? ElectricBottomLeft : ElectricBottomRight;
            edges.push_back(createEdge(
                edge, x, screen.y() + screen.height() - TOUCH_TARGET, TOUCH_TARGET, TOUCH_TARGET));
        }
        if (height <= corner_offset) {
            // An overlap with another output is near complete. We ignore this border.
            return;
        }
        edges.push_back(createEdge(border, x, y, TOUCH_TARGET, height));
    }

    std::unique_ptr<screen_edge<screen_edger>>
    createEdge(ElectricBorder border, int x, int y, int width, int height, bool createAction = true)
    {
        auto edge = space.create_screen_edge(*this);

        // Edges can not have negative size.
        assert(width >= 0);
        assert(height >= 0);

        edge->setBorder(border);
        edge->setGeometry(QRect(x, y, width, height));
        if (createAction) {
            ElectricBorderAction const action = actionForEdge(*edge);
            if (action != KWin::ElectricActionNone) {
                edge->reserve();
                edge->set_pointer_action(action);
            }
            ElectricBorderAction const touchAction = actionForTouchEdge(*edge);
            if (touchAction != KWin::ElectricActionNone) {
                edge->reserve();
                edge->set_touch_action(touchAction);
            }
        }
        if (desktop_switching.always) {
            if (edge->isCorner()) {
                edge->reserve();
            } else {
                if ((virtual_desktop_layout & Qt::Horizontal)
                    && (edge->isLeft() || edge->isRight())) {
                    edge->reserve();
                }
                if ((virtual_desktop_layout & Qt::Vertical)
                    && (edge->isTop() || edge->isBottom())) {
                    edge->reserve();
                }
            }
        }

        QObject::connect(edge->qobject.get(),
                         &screen_edge_qobject::approaching,
                         qobject.get(),
                         &screen_edger_qobject::approaching);
        QObject::connect(qobject.get(),
                         &screen_edger_qobject::checkBlocking,
                         edge->qobject.get(),
                         [edge = edge.get()] { edge->checkBlocking(); });

        return edge;
    }

    void setActionForBorder(ElectricBorder border,
                            ElectricBorderAction* oldValue,
                            ElectricBorderAction newValue)
    {
        if (*oldValue == newValue) {
            return;
        }
        if (*oldValue == ElectricActionNone) {
            // have to reserve
            for (auto& edge : edges) {
                if (edge->border == border) {
                    edge->reserve();
                }
            }
        }
        if (newValue == ElectricActionNone) {
            // have to unreserve
            for (auto& edge : edges) {
                if (edge->border == border) {
                    edge->unreserve();
                }
            }
        }
        *oldValue = newValue;
        // update action on all Edges for given border
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->set_pointer_action(newValue);
            }
        }
    }

    void setActionForTouchBorder(ElectricBorder border, ElectricBorderAction newValue)
    {
        auto it = touch_actions.find(border);
        ElectricBorderAction oldValue = ElectricActionNone;
        if (it != touch_actions.end()) {
            oldValue = it.value();
        }
        if (oldValue == newValue) {
            return;
        }
        if (oldValue == ElectricActionNone) {
            // have to reserve
            for (auto& edge : edges) {
                if (edge->border == border) {
                    edge->reserve();
                }
            }
        }
        if (newValue == ElectricActionNone) {
            // have to unreserve
            for (auto& edge : edges) {
                if (edge->border == border) {
                    edge->unreserve();
                }
            }

            touch_actions.erase(it);
        } else {
            touch_actions.insert(border, newValue);
        }
        // update action on all Edges for given border
        for (auto& edge : edges) {
            if (edge->border == border) {
                edge->set_touch_action(newValue);
            }
        }
    }

    void setRemainActiveOnFullscreen(bool remainActive)
    {
        m_remainActiveOnFullscreen = remainActive;
    }

    template<typename Edge>
    ElectricBorderAction actionForEdge(Edge& edge) const
    {
        switch (edge.border) {
        case ElectricTopLeft:
            return actions.top_left;
        case ElectricTop:
            return actions.top;
        case ElectricTopRight:
            return actions.top_right;
        case ElectricRight:
            return actions.right;
        case ElectricBottomRight:
            return actions.bottom_right;
        case ElectricBottom:
            return actions.bottom;
        case ElectricBottomLeft:
            return actions.bottom_left;
        case ElectricLeft:
            return actions.left;
        default:
            // fall through
            break;
        }
        return ElectricActionNone;
    }

    template<typename Edge>
    ElectricBorderAction actionForTouchEdge(Edge& edge) const
    {
        auto it = touch_actions.find(edge.border);
        if (it != touch_actions.end()) {
            return it.value();
        }
        return ElectricActionNone;
    }

    template<typename Win>
    void createEdgeForClient(Win* window, ElectricBorder border)
    {
        int y = 0;
        int x = 0;
        int width = 0;
        int height = 0;

        auto const& outputs = space.base.outputs;
        auto const geo = window->geo.frame;
        auto const fullArea = space_window_area(space, FullArea, 0, 1);

        for (auto output : outputs) {
            auto const screen = output->geometry();

            if (!screen.contains(geo)) {
                // ignoring Clients having a geometry overlapping with multiple screens
                // this would make the code more complex. If it's needed in future it can be added
                continue;
            }

            bool const bordersTop = (screen.y() == geo.y());
            bool const bordersLeft = (screen.x() == geo.x());
            bool const bordersBottom = (screen.y() + screen.height() == geo.y() + geo.height());
            bool const bordersRight = (screen.x() + screen.width() == geo.x() + geo.width());

            if (bordersTop && border == ElectricTop) {
                if (!isTopScreen(screen, fullArea)) {
                    continue;
                }
                y = geo.y();
                x = geo.x();
                height = 1;
                width = geo.width();
                break;
            }
            if (bordersBottom && border == ElectricBottom) {
                if (!isBottomScreen(screen, fullArea)) {
                    continue;
                }
                y = geo.y() + geo.height() - 1;
                x = geo.x();
                height = 1;
                width = geo.width();
                break;
            }
            if (bordersLeft && border == ElectricLeft) {
                if (!isLeftScreen(screen, fullArea)) {
                    continue;
                }
                x = geo.x();
                y = geo.y();
                width = 1;
                height = geo.height();
                break;
            }
            if (bordersRight && border == ElectricRight) {
                if (!isRightScreen(screen, fullArea)) {
                    continue;
                }
                x = geo.x() + geo.width() - 1;
                y = geo.y();
                width = 1;
                height = geo.height();
                break;
            }
        }

        if (width > 0 && height > 0) {
            auto edge = createEdge(border, x, y, width, height, false);
            edge->setClient(window);
            edge->reserve();
            edges.push_back(std::move(edge));
        } else {
            // we could not create an edge window, so don't allow the window to hide
            window->showOnScreenEdge();
        }
    }

    void deleteEdgeForClient(typename Space::window_t window)
    {
        auto it = edges.begin();
        while (it != edges.end()) {
            if ((*it)->client() == window) {
                it = edges.erase(it);
            } else {
                it++;
            }
        }
    }

    static ElectricBorderAction electricBorderAction(const QString& name)
    {
        QString lowerName = name.toLower();
        if (lowerName == QStringLiteral("showdesktop")) {
            return ElectricActionShowDesktop;
        } else if (lowerName == QStringLiteral("lockscreen")) {
            return ElectricActionLockScreen;
        } else if (lowerName == QLatin1String("krunner")) {
            return ElectricActionKRunner;
        } else if (lowerName == QLatin1String("applicationlauncher")) {
            return ElectricActionApplicationLauncher;
        }
        return ElectricActionNone;
    }

    bool isLeftScreen(QRect const& screen, QRect const& fullArea) const
    {
        auto const& outputs = space.base.outputs;

        if (outputs.size() == 1) {
            return true;
        }
        if (screen.x() == fullArea.x()) {
            return true;
        }

        // If any other screen has a right edge against our left edge, then this screen is not a
        // left screen.
        for (auto output : outputs) {
            auto const otherGeo = output->geometry();
            if (otherGeo == screen) {
                // that's our screen to test
                continue;
            }
            if (screen.x() == otherGeo.x() + otherGeo.width()
                && screen.y() < otherGeo.y() + otherGeo.height()
                && screen.y() + screen.height() > otherGeo.y()) {
                // There is a screen to the left.
                return false;
            }
        }

        // No screen exists to the left, so this is a left screen.
        return true;
    }

    bool isRightScreen(QRect const& screen, QRect const& fullArea) const
    {
        auto const& outputs = space.base.outputs;

        if (outputs.size() == 1) {
            return true;
        }
        if (screen.x() + screen.width() == fullArea.x() + fullArea.width()) {
            return true;
        }

        // If any other screen has any left edge against any of our right edge, then this screen is
        // not a right screen.
        for (auto output : outputs) {
            auto const otherGeo = output->geometry();
            if (otherGeo == screen) {
                // that's our screen to test
                continue;
            }
            if (screen.x() + screen.width() == otherGeo.x()
                && screen.y() < otherGeo.y() + otherGeo.height()
                && screen.y() + screen.height() > otherGeo.y()) {
                // There is a screen to the right.
                return false;
            }
        }

        // No screen exists to the right, so this is a right screen.
        return true;
    }

    bool isTopScreen(QRect const& screen, QRect const& fullArea) const
    {
        auto const& outputs = space.base.outputs;

        if (outputs.size() == 1) {
            return true;
        }
        if (screen.y() == fullArea.y()) {
            return true;
        }

        // If any other screen has any bottom edge against any of our top edge, then this screen is
        // not a top screen.
        for (auto output : outputs) {
            auto const otherGeo = output->geometry();
            if (otherGeo == screen) {
                // that's our screen to test
                continue;
            }
            if (screen.y() == otherGeo.y() + otherGeo.height()
                && screen.x() < otherGeo.x() + otherGeo.width()
                && screen.x() + screen.width() > otherGeo.x()) {
                // There is a screen to the top.
                return false;
            }
        }

        // No screen exists to the top, so this is a top screen.
        return true;
    }

    bool isBottomScreen(QRect const& screen, QRect const& fullArea) const
    {
        auto const& outputs = space.base.outputs;

        if (outputs.size() == 1) {
            return true;
        }
        if (screen.y() + screen.height() == fullArea.y() + fullArea.height()) {
            return true;
        }

        // If any other screen has any top edge against any of our bottom edge, then this screen is
        // not a bottom screen.
        for (auto output : outputs) {
            auto const otherGeo = output->geometry();
            if (otherGeo == screen) {
                // that's our screen to test
                continue;
            }

            if (screen.y() + screen.height() == otherGeo.y()
                && screen.x() < otherGeo.x() + otherGeo.width()
                && screen.x() + screen.width() > otherGeo.x()) {
                // There is a screen to the bottom.
                return false;
            }
        }

        // No screen exists to the bottom, so this is a bottom screen.
        return true;
    }

    Qt::Orientations virtual_desktop_layout{};

    QMap<ElectricBorder, ElectricBorderAction> touch_actions;
    bool m_remainActiveOnFullscreen{false};

    screen_edger_singleton singleton;
};

}
