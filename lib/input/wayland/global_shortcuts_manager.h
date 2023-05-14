/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/types.h"
#include "kwinglobals.h"
#include "win/input/gestures.h"
#include "win/input/global_shortcut.h"

#include <memory>

class QAction;
class KGlobalAccelD;

namespace KWin::input
{

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

    std::vector<win::KeyboardShortcut> get_keyboard_shortcut(QKeySequence const& seq);
    QList<QKeySequence> get_keyboard_shortcut(QAction* action);
    QList<QKeySequence> get_keyboard_shortcut(QString const& componentName,
                                              QString const& actionId);

    bool register_keyboard_default_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    bool register_keyboard_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    bool override_keyboard_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    void remove_keyboard_shortcut(QAction* action);
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
                              win::pointer_axis_direction axis);

    void registerTouchpadSwipe(win::swipe_direction direction,
                               uint fingerCount,
                               QAction* action,
                               std::function<void(qreal)> progressCallback);

    void registerTouchpadPinch(win::pinch_direction direction,
                               uint fingerCount,
                               QAction* action,
                               std::function<void(qreal)> progressCallback);

    void registerTouchscreenSwipe(QAction* action,
                                  std::function<void(qreal)> progressCallback,
                                  win::swipe_direction direction,
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
    bool processAxis(Qt::KeyboardModifiers modifiers, win::pointer_axis_direction axis);

    void processSwipeStart(win::input_device_type device, uint fingerCount);
    void processSwipeUpdate(win::input_device_type device, const QSizeF& delta);
    void processSwipeCancel(win::input_device_type device);
    void processSwipeEnd(win::input_device_type device);

    void processPinchStart(uint fingerCount);
    void processPinchUpdate(qreal scale, qreal angleDelta, const QSizeF& delta);
    void processPinchCancel();
    void processPinchEnd();

Q_SIGNALS:
    void keyboard_shortcut_changed(QAction* action, QKeySequence const& seq);

private:
    void objectDeleted(QObject* object);

    bool shortcut_exists(win::global_shortcut const& sc);
    void add_shortcut(win::global_shortcut sc);
    void add_gesture_shortcut(win::global_shortcut sc, win::input_device_type device);

    QVector<win::global_shortcut> m_shortcuts;

    std::unique_ptr<KGlobalAccelD> m_kglobalAccel;
    std::unique_ptr<win::gesture_recognizer> m_touchpadGestureRecognizer;
    std::unique_ptr<win::gesture_recognizer> m_touchscreenGestureRecognizer;
};

}
}
