/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"

#include "kwin_export.h"
#include "kwinglobals.h"

#include <QAction>
#include <QObject>
#include <QPoint>
#include <list>
#include <memory>
#include <vector>

class KGlobalAccelInterface;

namespace KWin
{
class Toplevel;

namespace input
{

class event_filter;
class event_spy;
class global_shortcuts_manager;
class platform;
class window_selector_filter;

class keyboard_redirect;
class pointer_redirect;
class tablet_redirect;
class touch_redirect;

/**
 * @brief This class is responsible for redirecting incoming input to the surface which currently
 * has input or send enter/leave events.
 *
 * In addition input is intercepted before passed to the surfaces to have KWin internal areas
 * getting input first (e.g. screen edges) and filter the input event out if we currently have
 * a full input grab.
 */
class KWIN_EXPORT redirect : public QObject
{
    Q_OBJECT
public:
    enum TabletEventType {
        Axis,
        Proximity,
        Tip,
    };

    ~redirect() override;

    /**
     * @return const QPointF& The current global pointer position
     */
    QPointF globalPointer() const;
    Qt::MouseButtons qtButtonStates() const;

    void registerShortcut(const QKeySequence& shortcut, QAction* action);
    /**
     * @overload
     *
     * Like registerShortcut, but also connects QAction::triggered to the @p slot on @p receiver.
     * It's recommended to use this method as it ensures that the X11 timestamp is updated prior
     * to the @p slot being invoked. If not using this overload it's required to ensure that
     * registerShortcut is called before connecting to QAction's triggered signal.
     */
    template<typename T, typename Slot>
    void registerShortcut(const QKeySequence& shortcut, QAction* action, T* receiver, Slot slot);
    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action);
    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action);
    void registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action);
    void registerGlobalAccel(KGlobalAccelInterface* interface);

    void cancelTouch();

    /**
     * Adds the @p filter to the list of event filters at the last relevant position.
     *
     * Install the filter at the back of the list for a X compositor, immediately before
     * the forward filter for a Wayland compositor.
     */
    void append_filter(event_filter* filter);
    /**
     * Adds the @p filter to the list of event filters and makes it the first
     * event filter in processing.
     *
     * Note: the event filter will get events before the lock screen can get them, thus
     * this is a security relevant method.
     */
    void prependInputEventFilter(event_filter* filter);
    void uninstallInputEventFilter(event_filter* filter);

    /**
     * Installs the @p spy for spying on events.
     */
    void installInputEventSpy(event_spy* spy);

    /**
     * Uninstalls the @p spy. This happens automatically when deleting an event_spy.
     */
    void uninstallInputEventSpy(event_spy* spy);

    Toplevel* findToplevel(const QPoint& pos);
    Toplevel* findManagedToplevel(const QPoint& pos);
    global_shortcuts_manager* shortcuts() const
    {
        return m_shortcuts.get();
    }

    /**
     * Sends an event through all InputFilters.
     * The method @p function is invoked on each input filter. Processing is stopped if
     * a filter returns @c true for @p function.
     *
     * The UnaryPredicate is defined like the UnaryPredicate of std::any_of.
     * The signature of the function should be equivalent to the following:
     * @code
     * bool function(event_filter const* filter);
     * @endcode
     *
     * The intended usage is to std::bind the method to invoke on the filter with all arguments
     * bind.
     */
    template<class UnaryPredicate>
    void processFilters(UnaryPredicate function)
    {
        std::any_of(m_filters.cbegin(), m_filters.cend(), function);
    }

    /**
     * Sends an event through all input event spies.
     * The @p function is invoked on each event_spy.
     *
     * The UnaryFunction is defined like the UnaryFunction of std::for_each.
     * The signature of the function should be equivalent to the following:
     * @code
     * void function(event_spy const* spy);
     * @endcode
     *
     * The intended usage is to std::bind the method to invoke on the spies with all arguments
     * bind.
     */
    template<class UnaryFunction>
    void processSpies(UnaryFunction function)
    {
        std::for_each(m_spies.cbegin(), m_spies.cend(), function);
    }

    keyboard_redirect* keyboard() const
    {
        return m_keyboard.get();
    }
    pointer_redirect* pointer() const
    {
        return m_pointer.get();
    }
    tablet_redirect* tablet() const
    {
        return m_tablet.get();
    }
    touch_redirect* touch() const
    {
        return m_touch.get();
    }

    virtual void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                                 QByteArray const& cursorName);
    virtual void startInteractivePositionSelection(std::function<void(QPoint const&)> callback);
    virtual bool isSelectingWindow() const;

    virtual void install_shortcuts() = 0;

    input::platform& platform;

Q_SIGNALS:
    /**
     * @brief Emitted when the global pointer position changed
     *
     * @param pos The new global pointer position.
     */
    void globalPointerChanged(const QPointF& pos);
    /**
     * @brief Emitted when the state of a pointer button changed.
     *
     * @param button The button which changed
     * @param state The new button state
     */
    void pointerButtonStateChanged(uint32_t button, button_state state);
    /**
     * @brief Emitted when the modifiers changes.
     *
     * Only emitted for the mask which is provided by Qt::KeyboardModifiers, if other modifiers
     * change signal is not emitted
     *
     * @param newMods The new modifiers state
     * @param oldMods The previous modifiers state
     */
    void keyboardModifiersChanged(Qt::KeyboardModifiers newMods, Qt::KeyboardModifiers oldMods);
    /**
     * @brief Emitted when the state of a key changed.
     *
     * @param keyCode The keycode of the key which changed
     * @param state The new key state
     */
    void keyStateChanged(quint32 keyCode, key_state state);

protected:
    redirect(input::platform& platform);

    std::unique_ptr<keyboard_redirect> m_keyboard;
    std::unique_ptr<pointer_redirect> m_pointer;
    std::unique_ptr<tablet_redirect> m_tablet;
    std::unique_ptr<touch_redirect> m_touch;

    std::unique_ptr<global_shortcuts_manager> m_shortcuts;

    std::list<event_filter*> m_filters;
    std::list<event_filter*>::const_iterator m_filters_install_iterator;

private:
    std::vector<event_spy*> m_spies;

    friend class DecorationEventFilter;
    friend class InternalWindowEventFilter;
    friend class ForwardInputFilter;
};

template<typename T, typename Slot>
inline void
redirect::registerShortcut(const QKeySequence& shortcut, QAction* action, T* receiver, Slot slot)
{
    registerShortcut(shortcut, action);
    QObject::connect(action, &QAction::triggered, receiver, slot);
}

}

}
