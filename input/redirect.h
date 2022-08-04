/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "redirect_qobject.h"

#include "kwin_export.h"

#include <QPoint>
#include <list>
#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace win
{
class space;
}

namespace input
{

class event_spy;
class platform;

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
class KWIN_EXPORT redirect
{
public:
    enum TabletEventType {
        Axis,
        Proximity,
        Tip,
    };

    virtual ~redirect();

    /**
     * @return const QPointF& The current global pointer position
     */
    QPointF globalPointer() const;
    Qt::MouseButtons qtButtonStates() const;

    void cancelTouch();

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

    virtual keyboard_redirect* get_keyboard() const = 0;
    virtual pointer_redirect* get_pointer() const = 0;
    virtual tablet_redirect* get_tablet() const = 0;
    virtual touch_redirect* get_touch() const = 0;

    virtual void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                                 QByteArray const& cursorName);
    virtual void startInteractivePositionSelection(std::function<void(QPoint const&)> callback);
    virtual bool isSelectingWindow() const;

    std::unique_ptr<redirect_qobject> qobject;
    input::platform& platform;
    win::space& space;

protected:
    redirect(input::platform& platform, win::space& space);

private:
    std::vector<event_spy*> m_spies;
};

}
}
