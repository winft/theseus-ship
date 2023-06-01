/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_manager.h"

#include "input/logging.h"
#include "kglobalaccel/runtime/global_accel_d.h"

#include <KGlobalAccel>
#include <QAction>

namespace KWin::input::wayland
{

using global_shortcut = win::global_shortcut;

global_shortcuts_manager::global_shortcuts_manager()
    : m_touchpadGestureRecognizer{std::make_unique<win::gesture_recognizer>()}
    , m_touchscreenGestureRecognizer{std::make_unique<win::gesture_recognizer>()}
{
}

global_shortcuts_manager::~global_shortcuts_manager() = default;

void global_shortcuts_manager::init()
{
    qputenv("KGLOBALACCELD_PLATFORM", QByteArrayLiteral("org.kde.kwin"));
    try {
        m_kglobalAccel = std::make_unique<KGlobalAccelD>();
        QObject::connect(KGlobalAccel::self(),
                         &KGlobalAccel::globalShortcutChanged,
                         this,
                         &global_shortcuts_manager::keyboard_shortcut_changed);
    } catch (std::runtime_error& exc) {
        qCWarning(KWIN_INPUT) << exc.what();
    }
}

std::vector<win::KeyboardShortcut>
global_shortcuts_manager::get_keyboard_shortcut(QKeySequence const& seq)
{
    return win::get_internal_shortcuts(KGlobalAccel::globalShortcutsByKey(seq));
}

QList<QKeySequence> global_shortcuts_manager::get_keyboard_shortcut(QAction* action)
{
    return KGlobalAccel::self()->shortcut(action);
}

QList<QKeySequence> global_shortcuts_manager::get_keyboard_shortcut(QString const& componentName,
                                                                    QString const& actionId)
{
    return KGlobalAccel::self()->globalShortcut(componentName, actionId);
}

bool global_shortcuts_manager::register_keyboard_default_shortcut(
    QAction* action,
    QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setDefaultShortcut(action, shortcut);
}

bool global_shortcuts_manager::register_keyboard_shortcut(QAction* action,
                                                          QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setShortcut(action, shortcut, KGlobalAccel::Autoloading);
}

bool global_shortcuts_manager::override_keyboard_shortcut(QAction* action,
                                                          QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setShortcut(action, shortcut, KGlobalAccel::NoAutoloading);
}

void global_shortcuts_manager::remove_keyboard_shortcut(QAction* action)
{
    KGlobalAccel::self()->removeAllShortcuts(action);
}

void global_shortcuts_manager::objectDeleted(QObject* object)
{
    auto it = m_shortcuts.begin();
    while (it != m_shortcuts.end()) {
        if (it->action() == object) {
            it = m_shortcuts.erase(it);
        } else {
            ++it;
        }
    }
}

bool global_shortcuts_manager::shortcut_exists(global_shortcut const& sc)
{
    for (const auto& cs : qAsConst(m_shortcuts)) {
        if (sc.shortcut() == cs.shortcut()) {
            return false;
        }
    }

    return true;
}

void global_shortcuts_manager::add_shortcut(global_shortcut sc)
{
    assert(!shortcut_exists(sc));

    QObject::connect(
        sc.action(), &QAction::destroyed, this, &global_shortcuts_manager::objectDeleted);
    m_shortcuts.push_back(std::move(sc));
}

void global_shortcuts_manager::add_gesture_shortcut(global_shortcut sc,
                                                    win::input_device_type device)
{
    assert(!shortcut_exists(sc));

    auto const& recognizer = device == win::input_device_type::touchpad
        ? m_touchpadGestureRecognizer
        : m_touchscreenGestureRecognizer;
    if (std::holds_alternative<win::RealtimeFeedbackSwipeShortcut>(sc.shortcut())) {
        recognizer->registerSwipeGesture(sc.swipeGesture());
    } else if (std::holds_alternative<win::RealtimeFeedbackPinchShortcut>(sc.shortcut())) {
        recognizer->registerPinchGesture(sc.pinchGesture());
    }

    add_shortcut(sc);
}

void global_shortcuts_manager::registerPointerShortcut(QAction* action,
                                                       Qt::KeyboardModifiers modifiers,
                                                       Qt::MouseButtons pointerButtons)
{
    auto sc = global_shortcut(win::PointerButtonShortcut{modifiers, pointerButtons}, action);
    if (!shortcut_exists(sc)) {
        add_shortcut(sc);
    }
}

void global_shortcuts_manager::registerAxisShortcut(QAction* action,
                                                    Qt::KeyboardModifiers modifiers,
                                                    win::pointer_axis_direction axis)
{
    auto sc = global_shortcut(win::PointerAxisShortcut{modifiers, axis}, action);
    if (!shortcut_exists(sc)) {
        add_shortcut(sc);
    }
}

void global_shortcuts_manager::registerTouchpadSwipe(win::swipe_direction direction,
                                                     uint fingerCount,
                                                     QAction* action,
                                                     std::function<void(qreal)> progressCallback)
{
    auto sc = global_shortcut(
        win::RealtimeFeedbackSwipeShortcut{
            win::input_device_type::touchpad, direction, progressCallback, fingerCount},
        action);
    if (!shortcut_exists(sc)) {
        add_gesture_shortcut(sc, win::input_device_type::touchpad);
    }
}

void global_shortcuts_manager::registerTouchpadPinch(win::pinch_direction direction,
                                                     uint fingerCount,
                                                     QAction* action,
                                                     std::function<void(qreal)> progressCallback)
{
    auto sc = global_shortcut(
        win::RealtimeFeedbackPinchShortcut{direction, progressCallback, fingerCount}, action);
    if (!shortcut_exists(sc)) {
        add_gesture_shortcut(sc, win::input_device_type::touchpad);
    }
}

void global_shortcuts_manager::registerTouchscreenSwipe(QAction* action,
                                                        std::function<void(qreal)> progressCallback,
                                                        win::swipe_direction direction,
                                                        uint fingerCount)
{
    auto sc = global_shortcut(
        win::RealtimeFeedbackSwipeShortcut{
            win::input_device_type::touchscreen, direction, progressCallback, fingerCount},
        action);
    if (!shortcut_exists(sc)) {
        add_gesture_shortcut(sc, win::input_device_type::touchscreen);
    }
}

bool global_shortcuts_manager::processKey(Qt::KeyboardModifiers mods, int keyQt)
{
    if (!keyQt && !mods) {
        return false;
    }

    auto check = [this](Qt::KeyboardModifiers mods, int keyQt) {
        return m_kglobalAccel->keyPressed(int(mods) | keyQt);
    };

    if (check(mods, keyQt)) {
        return true;
    }
    if (keyQt == Qt::Key_Backtab) {
        // KGlobalAccel on X11 has some workaround for Backtab
        // see kglobalaccel/src/runtime/plugins/xcb/kglobalccel_x11.cpp method x11KeyPress
        // Apparently KKeySequenceWidget captures Shift+Tab instead of Backtab
        // thus if the key is backtab we should adjust to add shift again and use tab
        // in addition KWin registers the shortcut incorrectly as Alt+Shift+Backtab
        // this should be changed to either Alt+Backtab or Alt+Shift+Tab to match
        // KKeySequenceWidget trying the variants
        if (check(mods | Qt::ShiftModifier, keyQt)) {
            return true;
        }
        if (check(mods | Qt::ShiftModifier, Qt::Key_Tab)) {
            return true;
        }
    }

    return false;
}

bool global_shortcuts_manager::processKeyRelease(Qt::KeyboardModifiers mods, int keyQt)
{
    m_kglobalAccel->keyReleased(int(mods) | keyQt);
    return false;
}

template<typename ShortcutKind, typename... Args>
bool match(QVector<global_shortcut>& shortcuts, Args... args)
{
    for (auto& sc : shortcuts) {
        if (std::holds_alternative<ShortcutKind>(sc.shortcut())) {
            if (std::get<ShortcutKind>(sc.shortcut()) == ShortcutKind{args...}) {
                sc.invoke();
                return true;
            }
        }
    }
    return false;
}

// TODO(C++20): use ranges for a nicer way of filtering by shortcut type
bool global_shortcuts_manager::processPointerPressed(Qt::KeyboardModifiers mods,
                                                     Qt::MouseButtons pointerButtons)
{
    return match<win::PointerButtonShortcut>(m_shortcuts, mods, pointerButtons);
}

bool global_shortcuts_manager::processAxis(Qt::KeyboardModifiers mods,
                                           win::pointer_axis_direction axis)
{
    return match<win::PointerAxisShortcut>(m_shortcuts, mods, axis);
}

void global_shortcuts_manager::processSwipeStart(win::input_device_type device, uint fingerCount)
{
    if (device == win::input_device_type::touchpad) {
        m_touchpadGestureRecognizer->startSwipeGesture(fingerCount);
    } else {
        m_touchscreenGestureRecognizer->startSwipeGesture(fingerCount);
    }
}

void global_shortcuts_manager::processSwipeUpdate(win::input_device_type device,
                                                  const QSizeF& delta)
{
    if (device == win::input_device_type::touchpad) {
        m_touchpadGestureRecognizer->updateSwipeGesture(delta);
    } else {
        m_touchscreenGestureRecognizer->updateSwipeGesture(delta);
    }
}

void global_shortcuts_manager::processSwipeCancel(win::input_device_type device)
{
    if (device == win::input_device_type::touchpad) {
        m_touchpadGestureRecognizer->cancelSwipeGesture();
    } else {
        m_touchscreenGestureRecognizer->cancelSwipeGesture();
    }
}

void global_shortcuts_manager::processSwipeEnd(win::input_device_type device)
{
    if (device == win::input_device_type::touchpad) {
        m_touchpadGestureRecognizer->endSwipeGesture();
    } else {
        m_touchscreenGestureRecognizer->endSwipeGesture();
    }
    // TODO: cancel on Wayland Seat if one triggered
}

void global_shortcuts_manager::processPinchStart(uint fingerCount)
{
    m_touchpadGestureRecognizer->startPinchGesture(fingerCount);
}

void global_shortcuts_manager::processPinchUpdate(qreal scale,
                                                  qreal angleDelta,
                                                  QSizeF const& delta)
{
    m_touchpadGestureRecognizer->updatePinchGesture(scale, angleDelta, delta);
}

void global_shortcuts_manager::processPinchCancel()
{
    m_touchpadGestureRecognizer->cancelPinchGesture();
}

void global_shortcuts_manager::processPinchEnd()
{
    m_touchpadGestureRecognizer->endPinchGesture();
}

}
