/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QPointer>
#include <QWindow>

namespace KWin
{
class Toplevel;

namespace Decoration
{
class DecoratedClientImpl;
}

namespace input
{
class redirect;

struct device_redirect_at {
    QPointer<Toplevel> at;
    QMetaObject::Connection surface_notifier;
};

struct device_redirect_focus {
    QPointer<Toplevel> focus;
    QPointer<Decoration::DecoratedClientImpl> decoration;
    QPointer<QWindow> internalWindow;
};

class KWIN_EXPORT device_redirect : public QObject
{
    Q_OBJECT
public:
    ~device_redirect() override;

    void update();

    /**
     * @brief First Toplevel currently at the position of the input device
     * according to the stacking order.
     * @return Toplevel* at device position.
     *
     * This will be null if no toplevel is at the position
     */
    Toplevel* at() const;
    /**
     * @brief Toplevel currently having pointer input focus (this might
     * be different from the Toplevel at the position of the pointer).
     * @return Toplevel* with pointer focus.
     *
     * This will be null if no toplevel has focus
     */
    Toplevel* focus() const;

    /**
     * @brief The Decoration currently receiving events.
     * @return decoration with pointer focus.
     */
    Decoration::DecoratedClientImpl* decoration() const;
    /**
     * @brief The internal window currently receiving events.
     * @return QWindow with pointer focus.
     */
    QWindow* internalWindow() const;

    virtual QPointF position() const
    {
        return {};
    }

    void setFocus(Toplevel* toplevel);
    void setDecoration(Decoration::DecoratedClientImpl* decoration);
    void setInternalWindow(QWindow* window);

Q_SIGNALS:
    void decorationChanged();

protected:
    device_redirect();

    virtual void cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
    {
    }
    virtual void cleanupDecoration(Decoration::DecoratedClientImpl* /*old*/,
                                   Decoration::DecoratedClientImpl* /*now*/)
    {
    }

    virtual void focusUpdate(Toplevel* /*old*/, Toplevel* /*now*/)
    {
    }

    /**
     * Certain input devices can be in a state of having no valid
     * position. An example are touch screens when no finger/pen
     * is resting on the surface (no touch point).
     */
    virtual bool positionValid() const
    {
        return true;
    }
    virtual bool focusUpdatesBlocked()
    {
        return false;
    }

    inline bool inited() const
    {
        return m_inited;
    }
    inline void setInited(bool set)
    {
        m_inited = set;
    }

private:
    bool setAt(Toplevel* toplevel);
    void updateFocus();
    bool updateDecoration();
    void updateInternalWindow(QWindow* window);

    QWindow* findInternalWindow(const QPoint& pos) const;

    device_redirect_at m_at;
    device_redirect_focus m_focus;

    bool m_inited = false;
};

}
}
