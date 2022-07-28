/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "redirect.h"

#include "input/tablet_redirect.h"

#include <QPointF>
#include <QSet>

namespace KWin
{

class Toplevel;

namespace input::wayland
{

class KWIN_EXPORT tablet_redirect : public input::tablet_redirect
{
    Q_OBJECT
public:
    explicit tablet_redirect(wayland::redirect* redirect);

    void init();

    QPointF position() const override;
    bool positionValid() const override;

    void tabletToolEvent(redirect::TabletEventType type,
                         QPointF const& pos,
                         qreal pressure,
                         int x_tilt,
                         int y_tilt,
                         qreal rotation,
                         bool tip_down,
                         bool tip_near,
                         quint64 serial_id,
                         quint64 toolId,
                         void* device) override;
    void tabletToolButtonEvent(uint button, bool isPressed) override;

    void tabletPadButtonEvent(uint button, bool isPressed) override;
    void tabletPadStripEvent(int number, int position, bool is_finger) override;
    void tabletPadRingEvent(int number, int position, bool is_finger) override;

    void cleanupDecoration(win::deco::client_impl* old, win::deco::client_impl* now) override;
    void cleanupInternalWindow(QWindow* old, QWindow* now) override;
    void focusUpdate(KWin::Toplevel* old, KWin::Toplevel* now) override;

    wayland::redirect* redirect;

private:
    struct {
        bool down = false;
        bool near = false;
    } tip;

    QPointF last_position;

    struct {
        QSet<uint> tool;
        QSet<uint> pad;
    } pressed_buttons;
};

}
}
