/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wl_visit.h"

#include "dnd.h"
#include "mime.h"
#include "sources.h"

#include "win/space.h"
#include "win/stacking_order.h"

namespace KWin::xwl
{

wl_visit::wl_visit(Toplevel* target, x11_source_ext& source)
    : qobject{std::make_unique<wl_visit_qobject>()}
    , target{target}
    , source{source}
{
    auto xcb_con = source.x11.connection;

    window = xcb_generate_id(xcb_con);
    uint32_t const dndValues[]
        = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};

    xcb_create_window(xcb_con,
                      XCB_COPY_FROM_PARENT,
                      window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      8192,
                      8192, // TODO: get current screen size and connect to changes
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      source.x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      dndValues);

    uint32_t version = drag_and_drop::version();
    xcb_change_property(xcb_con,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        source.x11.space->atoms->xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        &version);

    xcb_map_window(xcb_con, window);
    source.x11.space->stacking_order->manual_overlays.push_back(window);
    source.x11.space->stacking_order->update_count();

    xcb_flush(xcb_con);
    state.mapped = true;
}

wl_visit::~wl_visit()
{
    // TODO(romangg): Use the x11_data here. But we must ensure the Dnd object still exists at this
    //                point, i.e. use explicit ownership through smart pointer only.
    xcb_destroy_window(source.x11.connection, window);
    xcb_flush(source.x11.connection);
}

bool wl_visit::leave()
{
    unmap_proxy_window();
    return state.finished;
}

bool wl_visit::handle_client_message(xcb_client_message_event_t* event)
{
    if (event->window != window) {
        return false;
    }

    auto& atoms = source.x11.space->atoms;
    if (event->type == atoms->xdnd_enter) {
        return handle_enter(event);
    } else if (event->type == atoms->xdnd_position) {
        return handle_position(event);
    } else if (event->type == atoms->xdnd_drop) {
        return handle_drop(event);
    } else if (event->type == atoms->xdnd_leave) {
        return handle_leave(event);
    }
    return false;
}

static bool hasMimeName(mime_atoms const& mimes, std::string const& name)
{
    return std::any_of(mimes.begin(), mimes.end(), [name](auto const& m) { return m.id == name; });
}

bool wl_visit::handle_enter(xcb_client_message_event_t* event)
{
    if (state.entered) {
        // A drag already entered.
        return true;
    }

    state.entered = true;

    auto data = &event->data;
    source_window = data->data32[0];
    m_version = data->data32[1] >> 24;

    // get types
    mime_atoms offers;
    if (!(data->data32[1] & 1)) {
        // message has only max 3 types (which are directly in data)
        for (size_t i = 0; i < 3; i++) {
            xcb_atom_t mimeAtom = data->data32[2 + i];
            auto const mimeStrings = atom_to_mime_types(mimeAtom, *source.x11.space->atoms);
            for (auto const& mime : mimeStrings) {
                if (!hasMimeName(offers, mime)) {
                    offers.emplace_back(mime, mimeAtom);
                }
            }
        }
    } else {
        // more than 3 types -> in window property
        get_mimes_from_win_property(offers);
    }

    Q_EMIT qobject->offers_received(offers);
    return true;
}

void wl_visit::get_mimes_from_win_property(mime_atoms& offers)
{
    auto cookie = xcb_get_property(source.x11.connection,
                                   0,
                                   source_window,
                                   source.x11.space->atoms->xdnd_type_list,
                                   XCB_GET_PROPERTY_TYPE_ANY,
                                   0,
                                   0x1fffffff);

    auto reply = xcb_get_property_reply(source.x11.connection, cookie, nullptr);
    if (reply == nullptr) {
        return;
    }
    if (reply->type != XCB_ATOM_ATOM || reply->value_len == 0) {
        // invalid reply value
        free(reply);
        return;
    }

    auto mimeAtoms = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    for (size_t i = 0; i < reply->value_len; ++i) {
        auto const mimeStrings = atom_to_mime_types(mimeAtoms[i], *source.x11.space->atoms);
        for (auto const& mime : mimeStrings) {
            if (!hasMimeName(offers, mime)) {
                offers.emplace_back(mime, mimeAtoms[i]);
            }
        }
    }
    free(reply);
}

bool wl_visit::handle_position(xcb_client_message_event_t* event)
{
    auto data = &event->data;
    source_window = data->data32[0];

    if (!target) {
        // not over Wl window at the moment
        this->action = dnd_action::none;
        action_atom = XCB_ATOM_NONE;
        send_status();
        return true;
    }

    auto const pos = data->data32[2];
    Q_UNUSED(pos);

    source.timestamp = data->data32[3];

    auto& atoms = source.x11.space->atoms;
    xcb_atom_t actionAtom = m_version > 1 ? data->data32[4] : atoms->xdnd_action_copy;
    auto action = atom_to_client_action(actionAtom, *atoms);

    if (action == dnd_action::none) {
        // copy action is always possible in XDND
        action = dnd_action::copy;
        actionAtom = atoms->xdnd_action_copy;
    }

    if (this->action != action) {
        this->action = action;
        action_atom = actionAtom;
        source.get_source()->set_actions(action);
    }

    send_status();
    return true;
}

bool wl_visit::handle_drop(xcb_client_message_event_t* event)
{
    state.drop_handled = true;

    auto data = &event->data;
    source_window = data->data32[0];
    source.timestamp = data->data32[2];

    // We do nothing more here, the drop is being processed through the x11_source object.
    do_finish();
    return true;
}

void wl_visit::do_finish()
{
    state.finished = true;
    unmap_proxy_window();
    Q_EMIT qobject->finish();
}

bool wl_visit::handle_leave(xcb_client_message_event_t* event)
{
    state.entered = false;
    auto data = &event->data;
    source_window = data->data32[0];
    do_finish();
    return true;
}

void wl_visit::send_status()
{
    // Receive position events.
    uint32_t flags = 1 << 1;
    if (target_accepts_action()) {
        // accept the drop
        flags |= (1 << 0);
    }

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = window;
    data.data32[1] = flags;
    data.data32[4] = flags & (1 << 0) ? action_atom : static_cast<uint32_t>(XCB_ATOM_NONE);

    send_client_message(
        source.x11.connection, source_window, source.x11.space->atoms->xdnd_status, &data);
}

void wl_visit::send_finished()
{
    auto const accepted = state.entered && action != dnd_action::none;

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = window;
    data.data32[1] = accepted;
    data.data32[2] = accepted ? action_atom : static_cast<uint32_t>(XCB_ATOM_NONE);

    send_client_message(
        source.x11.connection, source_window, source.x11.space->atoms->xdnd_finished, &data);
}

bool wl_visit::target_accepts_action() const
{
    if (action == dnd_action::none) {
        return false;
    }
    auto const src_action = source.get_source()->action;
    return src_action == action || src_action == dnd_action::copy;
}

void wl_visit::unmap_proxy_window()
{
    if (!state.mapped) {
        return;
    }

    xcb_unmap_window(source.x11.connection, window);

    remove_all(source.x11.space->stacking_order->manual_overlays, window);
    source.x11.space->stacking_order->update_count();

    xcb_flush(source.x11.connection);
    state.mapped = false;
}

}
