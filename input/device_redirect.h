/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "win/deco/client_impl.h"

#include <QWindow>

namespace KWin::input
{

template<typename Window>
struct device_redirect_at {
    Window* window{nullptr};
    struct {
        QMetaObject::Connection surface;
        QMetaObject::Connection destroy;
    } notifiers;
};

template<typename Window>
struct device_redirect_focus {
    Window* window{nullptr};
    win::deco::client_impl<Window>* deco{nullptr};
    QWindow* internal_window{nullptr};
    struct {
        QMetaObject::Connection window_destroy;
        QMetaObject::Connection deco_destroy;
        QMetaObject::Connection internal_window_destroy;
    } notifiers;
};

class KWIN_EXPORT device_redirect_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void decorationChanged();
};

template<typename Redirect>
class device_redirect
{
public:
    std::unique_ptr<device_redirect_qobject> qobject;
    Redirect* redirect;

    using space_t = typename std::remove_reference_t<decltype(redirect->platform.base)>::space_t;
    using window_t = typename space_t::window_t;

    virtual ~device_redirect() = default;

    virtual QPointF position() const
    {
        return {};
    }

    virtual void cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
    {
    }

    virtual void cleanupDecoration(win::deco::client_impl<window_t>* /*old*/,
                                   win::deco::client_impl<window_t>* /*now*/)
    {
    }

    virtual void focusUpdate(window_t* /*old*/, window_t* /*now*/)
    {
    }

    /**
     * Certain input devices can be in a state of having no valid position. An example are touch
     * screens when no finger/pen is resting on the surface (no touch point).
     */
    virtual bool positionValid() const
    {
        return true;
    }

    virtual bool focusUpdatesBlocked()
    {
        return false;
    }

    /**
     * Element currently at the position of the input device according to the stacking order. Might
     * be null if no element is at the position.
     */
    device_redirect_at<window_t> at;

    /**
     * Element currently having pointer input focus (this might be different from the window
     * at the position of the pointer). Might be null if no element has focus.
     */
    device_redirect_focus<window_t> focus;

protected:
    explicit device_redirect(Redirect* redirect)
        : qobject{std::make_unique<device_redirect_qobject>()}
        , redirect{redirect}
    {
    }
};

}
