/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/pointer_redirect.h"

class QWindow;

namespace Wrapland::Server
{
class Surface;
}

namespace KWin
{
class Toplevel;

namespace input
{

class pointer;

namespace wayland
{

class redirect;

class KWIN_EXPORT pointer_redirect : public input::pointer_redirect
{
    Q_OBJECT
public:
    explicit pointer_redirect(wayland::redirect* redirect);
    void init();

    void updateAfterScreenChange() override;
    void warp(QPointF const& pos);

    QPointF pos() const override;
    Qt::MouseButtons buttons() const override;
    bool areButtonsPressed() const override;

    void setEffectsOverrideCursor(Qt::CursorShape shape) override;
    void removeEffectsOverrideCursor() override;
    void setWindowSelectionCursor(QByteArray const& shape) override;
    void removeWindowSelectionCursor() override;

    void updatePointerConstraints() override;

    void setEnableConstraints(bool set) override;

    bool isConstrained() const override;

    bool focusUpdatesBlocked() override;

    void process_motion(motion_event const& event) override;
    void process_motion_absolute(motion_absolute_event const& event) override;
    void processMotion(QPointF const& pos,
                       uint32_t time,
                       KWin::input::pointer* device = nullptr) override;

    void process_button(button_event const& event) override;
    void process_axis(axis_event const& event) override;

    void process_swipe_begin(swipe_begin_event const& event) override;
    void process_swipe_update(swipe_update_event const& event) override;
    void process_swipe_end(swipe_end_event const& event) override;

    void process_pinch_begin(pinch_begin_event const& event) override;
    void process_pinch_update(pinch_update_event const& event) override;
    void process_pinch_end(pinch_end_event const& event) override;

    void process_frame() override;

    void cleanupInternalWindow(QWindow* old, QWindow* now) override;
    void cleanupDecoration(win::deco::client_impl* old, win::deco::client_impl* now) override;

    void focusUpdate(Toplevel* focusOld, Toplevel* focusNow) override;
    QPointF position() const override;

    wayland::redirect* redirect;

private:
    void update_on_start_move_resize();
    void update_to_reset();
    void update_position(QPointF const& pos);
    void update_button(button_event const& event);
    void warp_xcb_on_surface_left(Wrapland::Server::Surface* surface);
    QPointF apply_pointer_confinement(QPointF const& pos) const;
    void disconnect_confined_pointer_region_connection();
    void disconnect_locked_pointer_destroyed_connection();
    void disconnect_pointer_constraints_connection();
    void break_pointer_constraints(Wrapland::Server::Surface* surface);

    QPointF m_pos;
    QHash<uint32_t, button_state> m_buttons;
    Qt::MouseButtons qt_buttons;

    struct {
        QMetaObject::Connection focus_geometry;
        QMetaObject::Connection internal_window;
        QMetaObject::Connection constraints;
        QMetaObject::Connection constraints_activated;
        QMetaObject::Connection confined_pointer_region;
        QMetaObject::Connection locked_pointer_destroyed;
        QMetaObject::Connection decoration_geometry;
    } notifiers;

    struct {
        bool confined{false};
        bool locked{false};
        bool enabled{true};
    } constraints;
};

}
}
}
