/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <memory>

class QAction;
class KGlobalAccelD;
class KGlobalAccelInterface;

namespace KWin::input
{

class gesture_recognizer;
class global_shortcut;

namespace wayland
{

/**
 * @brief Manager for the global shortcut system inside KWin.
 *
 * This class is responsible for holding all the global shortcuts and to process a key press event.
 * That is trigger a shortcut if there is a match.
 *
 * For internal shortcut handling (those which are delivered inside KWin) QActions are used and
 * triggered if the shortcut matches. For external shortcut handling a DBus interface is used.
 */
class KWIN_EXPORT global_shortcuts_manager : public QObject
{
    Q_OBJECT
public:
    global_shortcuts_manager();
    ~global_shortcuts_manager() override;
    void init();

    /**
     * @brief Registers an internal global pointer shortcut
     *
     * @param action The action to trigger if the shortcut is pressed
     * @param modifiers The modifiers which need to be hold to trigger the action
     * @param pointerButtons The pointer button which needs to be pressed
     */
    void registerPointerShortcut(QAction* action,
                                 Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButtons pointerButtons);
    /**
     * @brief Registers an internal global axis shortcut
     *
     * @param action The action to trigger if the shortcut is triggered
     * @param modifiers The modifiers which need to be hold to trigger the action
     * @param axis The pointer axis
     */
    void registerAxisShortcut(QAction* action,
                              Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis);

    void registerTouchpadSwipe(QAction* action, SwipeDirection direction, uint fingerCount = 4);

    void registerRealtimeTouchpadSwipe(QAction* onUp,
                                       std::function<void(qreal)> progressCallback,
                                       SwipeDirection direction,
                                       uint fingerCount = 4);

    void registerTouchpadPinch(QAction* action, PinchDirection direction, uint fingerCount = 4);

    void registerRealtimeTouchpadPinch(QAction* onUp,
                                       std::function<void(qreal)> progressCallback,
                                       PinchDirection direction,
                                       uint fingerCount = 4);

    void registerTouchscreenSwipe(QAction* action,
                                  std::function<void(qreal)> progressCallback,
                                  SwipeDirection direction,
                                  uint fingerCount);

    /**
     * @brief Processes a key event to decide whether a shortcut needs to be triggered.
     *
     * If a shortcut triggered this method returns @c true to indicate to the caller that the event
     * should not be further processed. If there is no shortcut which triggered for the key, then
     * @c false is returned.
     *
     * @param modifiers The current hold modifiers
     * @param keyQt The Qt::Key which got pressed
     * @return @c true if a shortcut triggered, @c false otherwise
     */
    bool processKey(Qt::KeyboardModifiers modifiers, int keyQt);
    bool processKeyRelease(Qt::KeyboardModifiers modifiers, int keyQt);
    bool processPointerPressed(Qt::KeyboardModifiers modifiers, Qt::MouseButtons pointerButtons);
    /**
     * @brief Processes a pointer axis event to decide whether a shortcut needs to be triggered.
     *
     * If a shortcut triggered this method returns @c true to indicate to the caller that the event
     * should not be further processed. If there is no shortcut which triggered for the key, then
     * @c false is returned.
     *
     * @param modifiers The current hold modifiers
     * @param axis The axis direction which has triggered this event
     * @return @c true if a shortcut triggered, @c false otherwise
     */
    bool processAxis(Qt::KeyboardModifiers modifiers, PointerAxisDirection axis);

    void processSwipeStart(DeviceType device, uint fingerCount);
    void processSwipeUpdate(DeviceType device, const QSizeF& delta);
    void processSwipeCancel(DeviceType device);
    void processSwipeEnd(DeviceType device);

    void processPinchStart(uint fingerCount);
    void processPinchUpdate(qreal scale, qreal angleDelta, const QSizeF& delta);
    void processPinchCancel();
    void processPinchEnd();

    void setKGlobalAccelInterface(KGlobalAccelInterface* interface)
    {
        m_kglobalAccelInterface = interface;
    }

private:
    void objectDeleted(QObject* object);

    bool shortcut_exists(global_shortcut const& sc);
    void add_shortcut(global_shortcut sc);
    void add_gesture_shortcut(global_shortcut sc, DeviceType device);

    QVector<global_shortcut> m_shortcuts;

    std::unique_ptr<KGlobalAccelD> m_kglobalAccel;
    KGlobalAccelInterface* m_kglobalAccelInterface = nullptr;
    std::unique_ptr<gesture_recognizer> m_touchpadGestureRecognizer;
    std::unique_ptr<gesture_recognizer> m_touchscreenGestureRecognizer;
};

}
}
