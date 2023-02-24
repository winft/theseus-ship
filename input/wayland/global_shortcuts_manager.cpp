/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_manager.h"

#include "input/gestures.h"
#include "input/global_shortcut.h"
#include "input/logging.h"

#include <KGlobalAccel/private/kglobalaccel_interface.h>
#include <KGlobalAccel/private/kglobalacceld.h>
#include <QAction>

namespace KWin::input::wayland
{

global_shortcuts_manager::global_shortcuts_manager()
    : m_touchpadGestureRecognizer{std::make_unique<gesture_recognizer>()}
    , m_touchscreenGestureRecognizer{std::make_unique<gesture_recognizer>()}
{
}

global_shortcuts_manager::~global_shortcuts_manager() = default;

void global_shortcuts_manager::init()
{
    qputenv("KGLOBALACCELD_PLATFORM", QByteArrayLiteral("org.kde.kwin"));
    m_kglobalAccel = std::make_unique<KGlobalAccelD>();
    if (!m_kglobalAccel->init()) {
        qCDebug(KWIN_INPUT) << "Init of kglobalaccel failed";
        m_kglobalAccel.reset();
    } else {
        qCDebug(KWIN_INPUT) << "KGlobalAcceld inited";
    }
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

bool global_shortcuts_manager::addIfNotExists(global_shortcut sc, DeviceType device)
{
    for (const auto& cs : qAsConst(m_shortcuts)) {
        if (sc.shortcut() == cs.shortcut()) {
            return false;
        }
    }

    auto const& recognizer = device == DeviceType::Touchpad ? m_touchpadGestureRecognizer
                                                            : m_touchscreenGestureRecognizer;
    if (std::holds_alternative<RealtimeFeedbackSwipeShortcut>(sc.shortcut())) {
        recognizer->registerSwipeGesture(sc.swipeGesture());
    } else if (std::holds_alternative<RealtimeFeedbackPinchShortcut>(sc.shortcut())) {
        recognizer->registerPinchGesture(sc.pinchGesture());
    }
    QObject::connect(
        sc.action(), &QAction::destroyed, this, &global_shortcuts_manager::objectDeleted);
    m_shortcuts.push_back(std::move(sc));
    return true;
}

void global_shortcuts_manager::registerPointerShortcut(QAction* action,
                                                       Qt::KeyboardModifiers modifiers,
                                                       Qt::MouseButtons pointerButtons)
{
    addIfNotExists(global_shortcut(PointerButtonShortcut{modifiers, pointerButtons}, action));
}

void global_shortcuts_manager::registerAxisShortcut(QAction* action,
                                                    Qt::KeyboardModifiers modifiers,
                                                    PointerAxisDirection axis)
{
    addIfNotExists(global_shortcut(PointerAxisShortcut{modifiers, axis}, action));
}

void global_shortcuts_manager::registerTouchpadSwipe(QAction* action,
                                                     SwipeDirection direction,
                                                     uint fingerCount)
{
    addIfNotExists(
        global_shortcut(
            RealtimeFeedbackSwipeShortcut{DeviceType::Touchpad, direction, {}, fingerCount},
            action),
        DeviceType::Touchpad);
}

void global_shortcuts_manager::registerRealtimeTouchpadSwipe(
    QAction* action,
    std::function<void(qreal)> progressCallback,
    SwipeDirection direction,
    uint fingerCount)
{
    addIfNotExists(global_shortcut(
                       RealtimeFeedbackSwipeShortcut{
                           DeviceType::Touchpad, direction, progressCallback, fingerCount},
                       action),
                   DeviceType::Touchpad);
}

void global_shortcuts_manager::registerTouchpadPinch(QAction* action,
                                                     PinchDirection direction,
                                                     uint fingerCount)
{
    addIfNotExists(
        global_shortcut(RealtimeFeedbackPinchShortcut{direction, {}, fingerCount}, action),
        DeviceType::Touchpad);
}

void global_shortcuts_manager::registerRealtimeTouchpadPinch(
    QAction* onUp,
    std::function<void(qreal)> progressCallback,
    PinchDirection direction,
    uint fingerCount)
{
    addIfNotExists(
        global_shortcut(RealtimeFeedbackPinchShortcut{direction, progressCallback, fingerCount},
                        onUp),
        DeviceType::Touchpad);
}

void global_shortcuts_manager::registerTouchscreenSwipe(QAction* action,
                                                        std::function<void(qreal)> progressCallback,
                                                        SwipeDirection direction,
                                                        uint fingerCount)
{
    addIfNotExists(global_shortcut(
                       RealtimeFeedbackSwipeShortcut{
                           DeviceType::Touchscreen, direction, progressCallback, fingerCount},
                       action),
                   DeviceType::Touchscreen);
}

bool global_shortcuts_manager::processKey(Qt::KeyboardModifiers mods, int keyQt)
{
    if (m_kglobalAccelInterface) {
        if (!keyQt && !mods) {
            return false;
        }
        auto check = [this](Qt::KeyboardModifiers mods, int keyQt) {
            bool retVal = false;
            QMetaObject::invokeMethod(m_kglobalAccelInterface,
                                      "checkKeyPressed",
                                      Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, retVal),
                                      Q_ARG(int, int(mods) | keyQt));
            return retVal;
        };
        if (check(mods, keyQt)) {
            return true;
        } else if (keyQt == Qt::Key_Backtab) {
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
    }
    return false;
}

bool global_shortcuts_manager::processKeyRelease(Qt::KeyboardModifiers mods, int keyQt)
{
    if (m_kglobalAccelInterface) {
        QMetaObject::invokeMethod(m_kglobalAccelInterface,
                                  "checkKeyReleased",
                                  Qt::DirectConnection,
                                  Q_ARG(int, int(mods) | keyQt));
    }
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
    return match<PointerButtonShortcut>(m_shortcuts, mods, pointerButtons);
}

bool global_shortcuts_manager::processAxis(Qt::KeyboardModifiers mods, PointerAxisDirection axis)
{
    return match<PointerAxisShortcut>(m_shortcuts, mods, axis);
}

void global_shortcuts_manager::processSwipeStart(DeviceType device, uint fingerCount)
{
    if (device == DeviceType::Touchpad) {
        m_touchpadGestureRecognizer->startSwipeGesture(fingerCount);
    } else {
        m_touchscreenGestureRecognizer->startSwipeGesture(fingerCount);
    }
}

void global_shortcuts_manager::processSwipeUpdate(DeviceType device, const QSizeF& delta)
{
    if (device == DeviceType::Touchpad) {
        m_touchpadGestureRecognizer->updateSwipeGesture(delta);
    } else {
        m_touchscreenGestureRecognizer->updateSwipeGesture(delta);
    }
}

void global_shortcuts_manager::processSwipeCancel(DeviceType device)
{
    if (device == DeviceType::Touchpad) {
        m_touchpadGestureRecognizer->cancelSwipeGesture();
    } else {
        m_touchscreenGestureRecognizer->cancelSwipeGesture();
    }
}

void global_shortcuts_manager::processSwipeEnd(DeviceType device)
{
    if (device == DeviceType::Touchpad) {
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
