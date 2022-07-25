/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edges.h"

#include "activation.h"
#include "move.h"
#include "space.h"

#include "input/cursor.h"
#include "input/gestures.h"
#include "main.h"
#include "render/compositor.h"
#include "render/effects.h"

// DBus generated
#include "screenlocker_interface.h"

#include <KConfigGroup>
#include <QAction>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMouseEvent>

namespace KWin::win
{

// Mouse should not move more than this many pixels
static int const DISTANCE_RESET = 30;

// How large the touch target of the area recognizing touch gestures is
static const int TOUCH_TARGET = 3;

// How far the user needs to swipe before triggering an action.
static const int MINIMUM_DELTA = 44;

screen_edge::screen_edge(screen_edger* edger)
    : QObject(edger)
    , edger(edger)
    , gesture{std::make_unique<input::swipe_gesture>()}
{
    gesture->setMinimumFingerCount(1);
    gesture->setMaximumFingerCount(1);

    QObject::connect(
        gesture.get(),
        &input::gesture::triggered,
        this,
        [this] {
            stopApproaching();
            if (window) {
                window->showOnScreenEdge();
                unreserve();
                return;
            }
            handleTouchAction();
            handleTouchCallback();
        },
        Qt::QueuedConnection);

    QObject::connect(
        gesture.get(), &input::swipe_gesture::started, this, &screen_edge::startApproaching);
    QObject::connect(
        gesture.get(), &input::swipe_gesture::cancelled, this, &screen_edge::stopApproaching);
    QObject::connect(gesture.get(), &input::swipe_gesture::progress, this, [this](qreal progress) {
        int factor = progress * 256.0f;
        if (last_approaching_factor != factor) {
            last_approaching_factor = factor;
            Q_EMIT approaching(border, last_approaching_factor / 256.0f, approach_geometry);
        }
    });

    QObject::connect(this, &screen_edge::activatesForTouchGestureChanged, this, [this] {
        if (reserved_count > 0) {
            if (activatesForTouchGesture()) {
                this->edger->gesture_recognizer->registerGesture(gesture.get());
            } else {
                this->edger->gesture_recognizer->unregisterGesture(gesture.get());
            }
        }
    });
}

screen_edge::~screen_edge() = default;

void screen_edge::reserve()
{
    reserved_count++;
    if (reserved_count == 1) {
        // got activated
        activate();
    }
}

void screen_edge::reserve(QObject* object, char const* slot)
{
    connect(object, &QObject::destroyed, this, qOverload<QObject*>(&screen_edge::unreserve));
    callbacks.insert(object, QByteArray(slot));
    reserve();
}

void screen_edge::reserveTouchCallBack(QAction* action)
{
    if (contains(touch_actions, action)) {
        return;
    }
    connect(action, &QAction::destroyed, this, [this, action] { unreserveTouchCallBack(action); });
    touch_actions.push_back(action);
    reserve();
}

void screen_edge::unreserveTouchCallBack(QAction* action)
{
    auto it = std::find_if(
        touch_actions.begin(), touch_actions.end(), [action](QAction* a) { return a == action; });
    if (it != touch_actions.end()) {
        touch_actions.erase(it);
        unreserve();
    }
}

void screen_edge::unreserve()
{
    reserved_count--;
    if (reserved_count == 0) {
        // got deactivated
        stopApproaching();
        deactivate();
    }
}
void screen_edge::unreserve(QObject* object)
{
    if (callbacks.remove(object) > 0) {
        disconnect(object, &QObject::destroyed, this, qOverload<QObject*>(&screen_edge::unreserve));
        unreserve();
    }
}

bool screen_edge::activatesForPointer() const
{
    if (window) {
        return true;
    }
    if (edger->desktop_switching.always) {
        return true;
    }
    if (edger->desktop_switching.when_moving_client) {
        auto c = edger->space.move_resize_window;
        if (c && !win::is_resize(c)) {
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

bool screen_edge::activatesForTouchGesture() const
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

bool screen_edge::triggersFor(QPoint const& cursorPos) const
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

bool screen_edge::check(QPoint const& cursorPos, QDateTime const& triggerTime, bool forceNoPushBack)
{
    if (!triggersFor(cursorPos)) {
        return false;
    }
    if (last_trigger_time.isValid()
        && last_trigger_time.msecsTo(triggerTime)
            < edger->reactivate_threshold - edger->time_threshold) {
        // still in cooldown
        return false;
    }

    // no pushback so we have to activate at once
    bool directActivate = forceNoPushBack || edger->cursor_push_back_distance.isNull() || window;
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

void screen_edge::markAsTriggered(QPoint const& cursorPos, QDateTime const& triggerTime)
{
    last_trigger_time = triggerTime;

    // invalidate
    last_reset_time = QDateTime();
    triggered_point = cursorPos;
}

bool screen_edge::canActivate(QPoint const& cursorPos, QDateTime const& triggerTime)
{
    // we check whether either the timer has explicitly been invalidated (successful trigger) or is
    // bigger than the reactivation threshold (activation "aborted", usually due to moving away the
    // cursor from the corner after successful activation) either condition means that "this is the
    // first event in a new attempt"
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

void screen_edge::handle(QPoint const& cursorPos)
{
    auto movingClient = edger->space.move_resize_window;

    if ((edger->desktop_switching.when_moving_client && movingClient
         && !win::is_resize(movingClient))
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
        window->showOnScreenEdge();
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

bool screen_edge::handleAction(ElectricBorderAction action)
{
    switch (action) {
    case ElectricActionShowDesktop: {
        set_showing_desktop(edger->space, !edger->space.showing_desktop);
        return true;
    }
    case ElectricActionLockScreen: { // Lock the screen
        OrgFreedesktopScreenSaverInterface interface(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                     QStringLiteral("/ScreenSaver"),
                                                     QDBusConnection::sessionBus());
        if (interface.isValid()) {
            interface.Lock();
        }
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

bool screen_edge::handleByCallback()
{
    if (callbacks.isEmpty()) {
        return false;
    }

    for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
        bool retVal = false;
        QMetaObject::invokeMethod(it.key(),
                                  it.value().constData(),
                                  Q_RETURN_ARG(bool, retVal),
                                  Q_ARG(ElectricBorder, border));
        if (retVal) {
            return true;
        }
    }

    return false;
}

void screen_edge::handleTouchCallback()
{
    if (touch_actions.empty()) {
        return;
    }
    touch_actions.front()->trigger();
}

void screen_edge::switchDesktop(QPoint const& cursorPos)
{
    QPoint pos(cursorPos);
    auto& vds = edger->space.virtual_desktop_manager;
    uint const oldDesktop = vds->current();
    uint desktop = oldDesktop;
    int const OFFSET = 2;

    if (isLeft()) {
        const uint interimDesktop = desktop;
        desktop = vds->toLeft(desktop, vds->isNavigationWrappingAround());
        if (desktop != interimDesktop)
            pos.setX(kwinApp()->get_base().topology.size.width() - 1 - OFFSET);
    } else if (isRight()) {
        const uint interimDesktop = desktop;
        desktop = vds->toRight(desktop, vds->isNavigationWrappingAround());
        if (desktop != interimDesktop)
            pos.setX(OFFSET);
    }

    if (isTop()) {
        const uint interimDesktop = desktop;
        desktop = vds->above(desktop, vds->isNavigationWrappingAround());
        if (desktop != interimDesktop)
            pos.setY(kwinApp()->get_base().topology.size.height() - 1 - OFFSET);
    } else if (isBottom()) {
        const uint interimDesktop = desktop;
        desktop = vds->below(desktop, vds->isNavigationWrappingAround());
        if (desktop != interimDesktop)
            pos.setY(OFFSET);
    }

    if (auto c = edger->space.move_resize_window) {
        if (c->control->rules().checkDesktop(desktop) != int(desktop)) {
            // user attempts to move a client to another desktop where it is ruleforced to not be
            return;
        }
    }

    vds->setCurrent(desktop);

    if (vds->current() != oldDesktop) {
        push_back_is_blocked = true;
        edger->space.input->platform.cursor->set_pos(pos);

        QSharedPointer<QMetaObject::Connection> me(new QMetaObject::Connection);
        *me = QObject::connect(QCoreApplication::eventDispatcher(),
                               &QAbstractEventDispatcher::aboutToBlock,
                               this,
                               [this, me]() {
                                   QObject::disconnect(*me);
                                   const_cast<QSharedPointer<QMetaObject::Connection>*>(&me)->reset(
                                       nullptr);
                                   push_back_is_blocked = false;
                               });
    }
}

void screen_edge::pushCursorBack(QPoint const& cursorPos)
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

    edger->space.input->platform.cursor->set_pos(x, y);
}

void screen_edge::setGeometry(QRect const& geometry)
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
        auto const& base = kwinApp()->get_base();
        auto output = base::get_nearest_output(base.get_outputs(), this->geometry.center());
        assert(output);
        gesture->setStartGeometry(this->geometry);
        gesture->setMinimumDelta(QSizeF(MINIMUM_DELTA, MINIMUM_DELTA) / output->scale());
    }
}

void screen_edge::checkBlocking()
{
    auto window = edger->space.active_client;
    auto const newValue = !edger->remainActiveOnFullscreen() && window
        && window->control->fullscreen() && window->frameGeometry().contains(geometry.center())
        && !(edger->space.render.effects
             && edger->space.render.effects->hasActiveFullScreenEffect());

    if (newValue == is_blocked) {
        return;
    }

    bool const wasTouch = activatesForTouchGesture();
    is_blocked = newValue;
    if (wasTouch != activatesForTouchGesture()) {
        Q_EMIT activatesForTouchGestureChanged();
    }
    doUpdateBlocking();
}

void screen_edge::doUpdateBlocking()
{
}

void screen_edge::doGeometryUpdate()
{
}

void screen_edge::activate()
{
    if (activatesForTouchGesture()) {
        edger->gesture_recognizer->registerGesture(gesture.get());
    }
    doActivate();
}

void screen_edge::doActivate()
{
}

void screen_edge::deactivate()
{
    edger->gesture_recognizer->unregisterGesture(gesture.get());
    doDeactivate();
}

void screen_edge::doDeactivate()
{
}

void screen_edge::startApproaching()
{
    if (is_approaching) {
        return;
    }
    is_approaching = true;
    doStartApproaching();
    last_approaching_factor = 0;
    Q_EMIT approaching(border, 0.0, approach_geometry);
}

void screen_edge::doStartApproaching()
{
}

void screen_edge::stopApproaching()
{
    if (!is_approaching) {
        return;
    }
    is_approaching = false;
    doStopApproaching();
    last_approaching_factor = 0;
    Q_EMIT approaching(border, 0.0, approach_geometry);
}

void screen_edge::doStopApproaching()
{
}

void screen_edge::updateApproaching(QPoint const& point)
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
            Q_EMIT approaching(border, last_approaching_factor / 256.0f, approach_geometry);
        }
    } else {
        stopApproaching();
    }
}

quint32 screen_edge::window_id() const
{
    return 0;
}

quint32 screen_edge::approachWindow() const
{
    return 0;
}

void screen_edge::setBorder(ElectricBorder border)
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

inline void screen_edge::set_pointer_action(ElectricBorderAction action)
{
    pointer_action = action;
}

void screen_edge::set_touch_action(ElectricBorderAction action)
{
    const bool wasTouch = activatesForTouchGesture();
    touch_action = action;
    if (wasTouch != activatesForTouchGesture()) {
        Q_EMIT activatesForTouchGestureChanged();
    }
}

void screen_edge::setClient(Toplevel* window)
{
    const bool wasTouch = activatesForTouchGesture();
    this->window = window;
    if (wasTouch != activatesForTouchGesture()) {
        Q_EMIT activatesForTouchGestureChanged();
    }
}

/**********************************************************
 * screen_edger
 *********************************************************/

screen_edger::screen_edger(win::space& space)
    : gesture_recognizer{std::make_unique<input::gesture_recognizer>()}
    , space{space}
{
    int const gridUnit = QFontMetrics(QFontDatabase::systemFont(QFontDatabase::GeneralFont))
                             .boundingRect(QLatin1Char('M'))
                             .height();
    corner_offset = 4 * gridUnit;

    config = kwinApp()->config();

    reconfigure();
    updateLayout();
    recreateEdges();

    QObject::connect(kwinApp()->options.get(),
                     &base::options::configChanged,
                     this,
                     &win::screen_edger::reconfigure);
    QObject::connect(space.virtual_desktop_manager.get(),
                     &virtual_desktop_manager::layoutChanged,
                     this,
                     &screen_edger::updateLayout);

    QObject::connect(space.qobject.get(),
                     &win::space_qobject::clientActivated,
                     this,
                     &win::screen_edger::checkBlocking);
    QObject::connect(space.qobject.get(),
                     &win::space_qobject::clientRemoved,
                     this,
                     &screen_edger::deleteEdgeForClient);
}

screen_edger::~screen_edger() = default;

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

void screen_edger::reconfigure()
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
    setActionForBorder(
        ElectricLeft, &actions.left, electricBorderAction(borderConfig.readEntry("Left", "None")));

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

void screen_edger::setActionForBorder(ElectricBorder border,
                                      ElectricBorderAction* oldValue,
                                      ElectricBorderAction newValue)
{
    if (*oldValue == newValue) {
        return;
    }
    if (*oldValue == ElectricActionNone) {
        // have to reserve
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            if ((*it)->border == border) {
                (*it)->reserve();
            }
        }
    }
    if (newValue == ElectricActionNone) {
        // have to unreserve
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            if ((*it)->border == border) {
                (*it)->unreserve();
            }
        }
    }
    *oldValue = newValue;
    // update action on all Edges for given border
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->set_pointer_action(newValue);
        }
    }
}

void screen_edger::setActionForTouchBorder(ElectricBorder border, ElectricBorderAction newValue)
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
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            if ((*it)->border == border) {
                (*it)->reserve();
            }
        }
    }
    if (newValue == ElectricActionNone) {
        // have to unreserve
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            if ((*it)->border == border) {
                (*it)->unreserve();
            }
        }

        touch_actions.erase(it);
    } else {
        touch_actions.insert(border, newValue);
    }
    // update action on all Edges for given border
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->set_touch_action(newValue);
        }
    }
}

void screen_edger::setRemainActiveOnFullscreen(bool remainActive)
{
    m_remainActiveOnFullscreen = remainActive;
}

void screen_edger::updateLayout()
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

static bool isLeftScreen(QRect const& screen, QRect const& fullArea)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (outputs.size() == 1) {
        return true;
    }
    if (screen.x() == fullArea.x()) {
        return true;
    }

    // If any other screen has a right edge against our left edge, then this screen is not a left
    // screen.
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

static bool isRightScreen(QRect const& screen, QRect const& fullArea)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (outputs.size() == 1) {
        return true;
    }
    if (screen.x() + screen.width() == fullArea.x() + fullArea.width()) {
        return true;
    }

    // If any other screen has any left edge against any of our right edge, then this screen is not
    // a right screen.
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

static bool isTopScreen(QRect const& screen, QRect const& fullArea)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (outputs.size() == 1) {
        return true;
    }
    if (screen.y() == fullArea.y()) {
        return true;
    }

    // If any other screen has any bottom edge against any of our top edge, then this screen is not
    // a top screen.
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

static bool isBottomScreen(QRect const& screen, QRect const& fullArea)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (outputs.size() == 1) {
        return true;
    }
    if (screen.y() + screen.height() == fullArea.y() + fullArea.height()) {
        return true;
    }

    // If any other screen has any top edge against any of our bottom edge, then this screen is not
    // a bottom screen.
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

void screen_edger::recreateEdges()
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    auto oldEdges = edges;
    edges.clear();
    auto const fullArea = QRect({}, kwinApp()->get_base().topology.size);
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
                oldEdge->client()->showOnScreenEdge();
                continue;
            }
            if (oldEdge->border != edge->border) {
                continue;
            }
            const QHash<QObject*, QByteArray>& callbacks = oldEdge->callbacks;
            for (QHash<QObject*, QByteArray>::const_iterator callback = callbacks.begin();
                 callback != callbacks.end();
                 ++callback) {
                edge->reserve(callback.key(), callback.value().constData());
            }
            const auto touchCallBacks = oldEdge->touch_actions;
            for (auto a : touchCallBacks) {
                edge->reserveTouchCallBack(a);
            }
        }
    }
    qDeleteAll(oldEdges);
}

void screen_edger::setDesktopSwitching(bool enable)
{
    if (enable == desktop_switching.always) {
        return;
    }
    desktop_switching.always = enable;
    reserveDesktopSwitching(enable, virtual_desktop_layout);
}

void screen_edger::createVerticalEdge(ElectricBorder border,
                                      QRect const& screen,
                                      QRect const& fullArea)
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
        ElectricBorder const edge = (border == ElectricLeft) ? ElectricTopLeft : ElectricTopRight;
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

void screen_edger::createHorizontalEdge(ElectricBorder border,
                                        QRect const& screen,
                                        QRect const& fullArea)
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

screen_edge* screen_edger::createEdge(ElectricBorder border,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      bool createAction)
{
    auto edge = space.create_screen_edge(*this);

    // Edges can not have negative size.
    assert(width >= 0);
    assert(height >= 0);

    edge->setBorder(border);
    edge->setGeometry(QRect(x, y, width, height));
    if (createAction) {
        ElectricBorderAction const action = actionForEdge(edge);
        if (action != KWin::ElectricActionNone) {
            edge->reserve();
            edge->set_pointer_action(action);
        }
        ElectricBorderAction const touchAction = actionForTouchEdge(edge);
        if (touchAction != KWin::ElectricActionNone) {
            edge->reserve();
            edge->set_touch_action(touchAction);
        }
    }
    if (desktop_switching.always) {
        if (edge->isCorner()) {
            edge->reserve();
        } else {
            if ((virtual_desktop_layout & Qt::Horizontal) && (edge->isLeft() || edge->isRight())) {
                edge->reserve();
            }
            if ((virtual_desktop_layout & Qt::Vertical) && (edge->isTop() || edge->isBottom())) {
                edge->reserve();
            }
        }
    }

    connect(edge, &screen_edge::approaching, this, &screen_edger::approaching);
    connect(this, &screen_edger::checkBlocking, edge, &screen_edge::checkBlocking);

    return edge;
}

ElectricBorderAction screen_edger::actionForEdge(screen_edge* edge) const
{
    switch (edge->border) {
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

ElectricBorderAction screen_edger::actionForTouchEdge(screen_edge* edge) const
{
    auto it = touch_actions.find(edge->border);
    if (it != touch_actions.end()) {
        return it.value();
    }
    return ElectricActionNone;
}

void screen_edger::reserveDesktopSwitching(bool isToReserve, Qt::Orientations o)
{
    if (!o)
        return;
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        auto edge = *it;
        if (edge->isCorner()) {
            isToReserve ? edge->reserve() : edge->unreserve();
        } else {
            if ((virtual_desktop_layout & Qt::Horizontal) && (edge->isLeft() || edge->isRight())) {
                isToReserve ? edge->reserve() : edge->unreserve();
            }
            if ((virtual_desktop_layout & Qt::Vertical) && (edge->isTop() || edge->isBottom())) {
                isToReserve ? edge->reserve() : edge->unreserve();
            }
        }
    }
}

void screen_edger::reserve(ElectricBorder border, QObject* object, const char* slot)
{
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->reserve(object, slot);
        }
    }
}

void screen_edger::unreserve(ElectricBorder border, QObject* object)
{
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->unreserve(object);
        }
    }
}

void screen_edger::reserve(Toplevel* window, ElectricBorder border)
{
    bool hadBorder = false;
    auto it = edges.begin();

    while (it != edges.end()) {
        if ((*it)->client() == window) {
            hadBorder = true;
            delete *it;
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

void screen_edger::reserveTouch(ElectricBorder border, QAction* action)
{
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->reserveTouchCallBack(action);
        }
    }
}

void screen_edger::unreserveTouch(ElectricBorder border, QAction* action)
{
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->border == border) {
            (*it)->unreserveTouchCallBack(action);
        }
    }
}

void screen_edger::createEdgeForClient(Toplevel* window, ElectricBorder border)
{
    int y = 0;
    int x = 0;
    int width = 0;
    int height = 0;

    auto const& outputs = kwinApp()->get_base().get_outputs();
    QRect const geo = window->frameGeometry();
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
        edges.push_back(edge);
        edge->reserve();
    } else {
        // we could not create an edge window, so don't allow the window to hide
        window->showOnScreenEdge();
    }
}

void screen_edger::deleteEdgeForClient(Toplevel* window)
{
    auto it = edges.begin();
    while (it != edges.end()) {
        if ((*it)->client() == window) {
            delete *it;
            it = edges.erase(it);
        } else {
            it++;
        }
    }
}

void screen_edger::check(QPoint const& pos, QDateTime const& now, bool forceNoPushBack)
{
    bool activatedForClient = false;

    for (auto it = edges.begin(); it != edges.end(); ++it) {
        if ((*it)->reserved_count == 0) {
            continue;
        }
        if (!(*it)->activatesForPointer()) {
            continue;
        }
        if ((*it)->approach_geometry.contains(pos)) {
            (*it)->startApproaching();
        }
        if ((*it)->client() != nullptr && activatedForClient) {
            (*it)->markAsTriggered(pos, now);
            continue;
        }
        if ((*it)->check(pos, now, forceNoPushBack)) {
            if ((*it)->client()) {
                activatedForClient = true;
            }
        }
    }
}

bool screen_edger::isEntered(QMouseEvent* event)
{
    assert(event->type() == QEvent::MouseMove);

    bool activated = false;
    bool activatedForClient = false;

    for (auto it = edges.begin(); it != edges.end(); ++it) {
        auto edge = *it;
        if (edge->reserved_count == 0) {
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
                edge->markAsTriggered(event->globalPos(),
                                      QDateTime::fromMSecsSinceEpoch(event->timestamp(), Qt::UTC));
            }
        }
    }

    return activated;
}

bool screen_edger::handleEnterNotifiy(xcb_window_t window,
                                      QPoint const& point,
                                      QDateTime const& timestamp)
{
    bool activated = false;
    bool activatedForClient = false;

    for (auto it = edges.begin(); it != edges.end(); ++it) {
        auto edge = *it;
        if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
            continue;
        }
        if (edge->reserved_count == 0) {
            continue;
        }
        if (!edge->activatesForPointer()) {
            continue;
        }

        if (edge->window_id() == window) {
            if (edge->check(point, timestamp)) {
                if ((*it)->client()) {
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

bool screen_edger::handleDndNotify(xcb_window_t window, QPoint const& point)
{
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        auto edge = *it;
        if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
            continue;
        }
        if (edge->reserved_count > 0 && edge->window_id() == window) {
            kwinApp()->update_x11_time_from_clock();
            edge->check(point, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC), true);
            return true;
        }
    }
    return false;
}
bool screen_edger::remainActiveOnFullscreen() const
{
    return m_remainActiveOnFullscreen;
}

void screen_edger::ensureOnTop()
{
    base::x11::xcb::restack_windows_with_raise(windows());
}

std::vector<xcb_window_t> screen_edger::windows() const
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

}
