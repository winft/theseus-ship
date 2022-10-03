/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "drag.h"
#include "mime.h"
#include "sources.h"

#include "base/wayland/server.h"
#include "win/space.h"

#include <QObject>
#include <QPoint>
#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <memory>

namespace KWin::xwl
{

class KWIN_EXPORT x11_visit_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void finish();
};

template<typename Space>
class x11_visit
{
public:
    // TODO: handle ask action
    using window_t = typename Space::x11_window;

    x11_visit(window_t* target,
              wl_source<Wrapland::Server::data_source, Space> const& source,
              xcb_window_t drag_window)
        : qobject{std::make_unique<x11_visit_qobject>()}
        , target(target)
        , source{source}
        , drag_window{drag_window}
    {
        // first check supported DND version
        auto xcb_con = source.core.x11.connection;
        auto cookie = xcb_get_property(xcb_con,
                                       0,
                                       target->xcb_window,
                                       source.core.x11.atoms->xdnd_aware,
                                       XCB_GET_PROPERTY_TYPE_ANY,
                                       0,
                                       1);

        auto reply = xcb_get_property_reply(xcb_con, cookie, nullptr);
        if (!reply) {
            do_finish();
            return;
        }

        if (reply->type != XCB_ATOM_ATOM) {
            do_finish();
            free(reply);
            return;
        }

        auto value = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
        version = qMin(*value, drag_and_drop_version);
        if (version < 1) {
            // minimal version we accept is 1
            do_finish();
            free(reply);
            return;
        }
        free(reply);

        // proxy drop
        receive_offer();

        notifiers.drop = QObject::connect(waylandServer()->seat(),
                                          &Wrapland::Server::Seat::dragEnded,
                                          qobject.get(),
                                          [this](auto success) {
                                              if (success) {
                                                  drop();
                                              } else {
                                                  leave();
                                              }
                                          });
    }

    bool handle_client_message(xcb_client_message_event_t* event)
    {
        auto& atoms = source.core.x11.atoms;
        if (event->type == atoms->xdnd_status) {
            return handle_status(event);
        } else if (event->type == atoms->xdnd_finished) {
            return handle_finished(event);
        }
        return false;
    }

    void send_position(QPointF const& globalPos)
    {
        int16_t const x = globalPos.x();
        int16_t const y = globalPos.y();

        if (m_pos.pending) {
            m_pos.cache = QPoint(x, y);
            m_pos.cached = true;
            return;
        }

        m_pos.pending = true;

        xcb_client_message_data_t data = {{0}};
        data.data32[0] = drag_window;
        data.data32[2] = (x << 16) | y;
        data.data32[3] = XCB_CURRENT_TIME;
        data.data32[4] = client_action_to_atom(actions.proposed, *source.core.x11.atoms);

        send_client_message(source.core.x11.connection,
                            target->xcb_window,
                            source.core.x11.atoms->xdnd_position,
                            &data);
    }

    void leave()
    {
        Q_ASSERT(!state.dropped);
        if (state.finished) {
            // Was already finished.
            return;
        }
        // We only need to leave if we entered before.
        if (state.entered) {
            send_leave();
        }
        do_finish();
    }

    std::unique_ptr<x11_visit_qobject> qobject;

    window_t* target;

    struct {
        bool entered{false};
        bool dropped{false};
        bool finished{false};
    } state;

private:
    bool handle_status(xcb_client_message_event_t* event)
    {
        auto data = &event->data;
        if (data->data32[0] != target->xcb_window) {
            // wrong target window
            return false;
        }

        m_accepts = data->data32[1] & 1;
        xcb_atom_t actionAtom = data->data32[4];

        // TODO: we could optimize via rectangle in data32[2] and data32[3]

        // position round trip finished
        m_pos.pending = false;

        if (!state.dropped) {
            // as long as the drop is not yet done determine requested action
            actions.preferred = atom_to_client_action(actionAtom, *source.core.x11.atoms);
            update_actions();
        }

        if (m_pos.cached) {
            // send cached position
            m_pos.cached = false;
            send_position(m_pos.cache);
        } else if (state.dropped) {
            // drop was done in between, now close it out
            drop();
        }
        return true;
    }

    bool handle_finished(xcb_client_message_event_t* event)
    {
        auto data = &event->data;

        if (data->data32[0] != target->xcb_window) {
            // different target window
            return false;
        }

        if (!state.dropped) {
            // drop was never done
            do_finish();
            return true;
        }

        auto const success = version > 4 ? data->data32[1] & 1 : true;
        xcb_atom_t const usedActionAtom
            = version > 4 ? data->data32[2] : static_cast<uint32_t>(XCB_ATOM_NONE);
        Q_UNUSED(success);
        Q_UNUSED(usedActionAtom);

        do_finish();
        return true;
    }

    void send_enter()
    {
        xcb_client_message_data_t data = {{0}};
        data.data32[0] = drag_window;
        data.data32[1] = version << 24;

        auto const mimeTypesNames = source.server_source->mime_types();
        auto const mimesCount = mimeTypesNames.size();
        size_t cnt = 0;
        size_t totalCnt = 0;

        for (auto const& mimeName : mimeTypesNames) {
            // 3 mimes and less can be sent directly in the XdndEnter message
            if (totalCnt == 3) {
                break;
            }
            auto const atom = mime_type_to_atom(mimeName.c_str(), *source.core.x11.atoms);

            if (atom != XCB_ATOM_NONE) {
                data.data32[cnt + 2] = atom;
                cnt++;
            }
            totalCnt++;
        }

        for (int i = cnt; i < 3; i++) {
            data.data32[i + 2] = XCB_ATOM_NONE;
        }

        if (mimesCount > 3) {
            // need to first transfer all available mime types
            data.data32[1] |= 1;

            std::vector<xcb_atom_t> targets;
            targets.resize(mimesCount);

            size_t cnt = 0;
            for (auto const& mimeName : mimeTypesNames) {
                auto const atom = mime_type_to_atom(mimeName.c_str(), *source.core.x11.atoms);
                if (atom != XCB_ATOM_NONE) {
                    targets[cnt] = atom;
                    cnt++;
                }
            }

            xcb_change_property(source.core.x11.connection,
                                XCB_PROP_MODE_REPLACE,
                                drag_window,
                                source.core.x11.atoms->xdnd_type_list,
                                XCB_ATOM_ATOM,
                                32,
                                cnt,
                                targets.data());
        }

        send_client_message(source.core.x11.connection,
                            target->xcb_window,
                            source.core.x11.atoms->xdnd_enter,
                            &data);
    }

    void send_drop(uint32_t time)
    {
        xcb_client_message_data_t data = {{0}};
        data.data32[0] = drag_window;
        data.data32[2] = time;

        send_client_message(source.core.x11.connection,
                            target->xcb_window,
                            source.core.x11.atoms->xdnd_drop,
                            &data);

        if (version < 2) {
            do_finish();
        }
    }

    void send_leave()
    {
        xcb_client_message_data_t data = {{0}};
        data.data32[0] = drag_window;

        send_client_message(source.core.x11.connection,
                            target->xcb_window,
                            source.core.x11.atoms->xdnd_leave,
                            &data);
    }

    void receive_offer()
    {
        if (state.finished) {
            // Already ended.
            return;
        }

        enter();
        update_actions();

        notifiers.action
            = QObject::connect(source.server_source,
                               &Wrapland::Server::data_source::supported_dnd_actions_changed,
                               qobject.get(),
                               [this] { update_actions(); });

        send_position(waylandServer()->seat()->pointers().get_position());
    }

    void enter()
    {
        state.entered = true;

        // Send enter event and current position to X client.
        send_enter();

        // Proxy future pointer position changes.
        notifiers.motion = QObject::connect(waylandServer()->seat(),
                                            &Wrapland::Server::Seat::pointerPosChanged,
                                            qobject.get(),
                                            [this](auto const& pos) { send_position(pos); });
    }

    void update_actions()
    {
        auto const old_proposed = actions.proposed;
        auto const supported = source.server_source->supported_dnd_actions();

        if (supported.testFlag(actions.preferred)) {
            actions.proposed = actions.preferred;
        } else if (supported.testFlag(dnd_action::copy)) {
            actions.proposed = dnd_action::copy;
        } else {
            actions.proposed = dnd_action::none;
        }

        // Send updated action to X target.
        if (old_proposed != actions.proposed) {
            send_position(waylandServer()->seat()->pointers().get_position());
        }

        auto const pref
            = actions.preferred != dnd_action::none ? actions.preferred : dnd_action::copy;

        // We assume the X client supports Move, but this might be wrong - then the drag just
        // cancels, if the user tries to force it.
        waylandServer()->seat()->drags().target_actions_update(
            QFlags({dnd_action::copy, dnd_action::move}), pref);
    }

    void drop()
    {
        Q_ASSERT(!state.finished);
        state.dropped = true;

        // Stop further updates.
        // TODO(romangg): revisit when we allow ask action
        stop_connections();

        if (!state.entered) {
            // wait for enter (init + offers)
            return;
        }
        if (m_pos.pending) {
            // wait for pending position roundtrip
            return;
        }
        if (!m_accepts) {
            // target does not accept current action/offer
            send_leave();
            do_finish();
            return;
        }

        // Dnd session ended successfully.
        send_drop(XCB_CURRENT_TIME);
    }

    void do_finish()
    {
        state.finished = true;
        m_pos.cached = false;
        stop_connections();
        Q_EMIT qobject->finish();
    }

    void stop_connections()
    {
        // Final outcome has been determined from Wayland side, no more updates needed.
        QObject::disconnect(notifiers.drop);
        notifiers.drop = QMetaObject::Connection();

        QObject::disconnect(notifiers.motion);
        notifiers.motion = QMetaObject::Connection();
        QObject::disconnect(notifiers.action);
        notifiers.action = QMetaObject::Connection();
    }

    wl_source<Wrapland::Server::data_source, Space> const& source;
    xcb_window_t drag_window;
    uint32_t version = 0;

    struct {
        QMetaObject::Connection motion;
        QMetaObject::Connection action;
        QMetaObject::Connection drop;
    } notifiers;

    struct {
        bool pending = false;
        bool cached = false;
        QPoint cache;
    } m_pos;

    struct {
        // Preferred by the X client.
        dnd_action preferred{dnd_action::none};
        // Decided upon by the compositor.
        dnd_action proposed{dnd_action::none};
    } actions;

    bool m_accepts = false;
};

}
