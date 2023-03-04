/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "selection_owner.h"

#include <QAbstractNativeEventFilter>
#include <QBasicTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QTimerEvent>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <private/qtx11extras_p.h>
#else
#include <QX11Info>
#endif

namespace KWin::base::x11
{

static xcb_window_t get_selection_owner(xcb_connection_t* c, xcb_atom_t selection)
{
    xcb_window_t owner = XCB_NONE;
    xcb_get_selection_owner_reply_t* reply
        = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, selection), nullptr);

    if (reply) {
        owner = reply->owner;
        free(reply);
    }

    return owner;
}

static xcb_atom_t intern_atom(xcb_connection_t* c, char const* name)
{
    xcb_atom_t atom = XCB_NONE;
    xcb_intern_atom_reply_t* reply
        = xcb_intern_atom_reply(c, xcb_intern_atom(c, false, strlen(name), name), nullptr);

    if (reply) {
        atom = reply->atom;
        free(reply);
    }

    return atom;
}

class Q_DECL_HIDDEN selection_owner::Private : public QAbstractNativeEventFilter
{
public:
    enum State { Idle, WaitingForTimestamp, WaitingForPreviousOwner };

    Private(selection_owner* owner_P,
            xcb_atom_t selection_P,
            xcb_connection_t* c,
            xcb_window_t root)
        : state(Idle)
        , selection(selection_P)
        , connection(c)
        , root(root)
        , window(XCB_NONE)
        , prev_owner(XCB_NONE)
        , timestamp(XCB_CURRENT_TIME)
        , extra1(0)
        , extra2(0)
        , force_kill(false)
        , owner(owner_P)
    {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    void claimSucceeded();
    void gotTimestamp();
    void timeout();

    State state;
    xcb_atom_t const selection;
    xcb_connection_t* connection;
    xcb_window_t root;
    xcb_window_t window;
    xcb_window_t prev_owner;
    xcb_timestamp_t timestamp;
    uint32_t extra1, extra2;
    QBasicTimer timer;
    bool force_kill;
    static xcb_atom_t manager_atom;
    static xcb_atom_t xa_multiple;
    static xcb_atom_t xa_targets;
    static xcb_atom_t xa_timestamp;

    static std::unique_ptr<Private>
    create(selection_owner* owner, xcb_atom_t selection_P, int screen_P);
    static std::unique_ptr<Private>
    create(selection_owner* owner, char const* selection_P, int screen_P);
    static std::unique_ptr<Private>
    create(selection_owner* owner, xcb_atom_t selection_P, xcb_connection_t* c, xcb_window_t root);
    static std::unique_ptr<Private>
    create(selection_owner* owner, char const* selection_P, xcb_connection_t* c, xcb_window_t root);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(QByteArray const& eventType, void* message, qintptr*) override
#else
    bool nativeEventFilter(QByteArray const& eventType, void* message, long*) override
#endif
    {
        if (eventType != "xcb_generic_event_t") {
            return false;
        }
        return owner->filterEvent(message);
    }

private:
    selection_owner* owner;
};

std::unique_ptr<selection_owner::Private>
selection_owner::Private::create(selection_owner* owner, xcb_atom_t selection_P, int screen_P)
{
    return create(owner, selection_P, QX11Info::connection(), QX11Info::appRootWindow(screen_P));
}

std::unique_ptr<selection_owner::Private> selection_owner::Private::create(selection_owner* owner,
                                                                           xcb_atom_t selection_P,
                                                                           xcb_connection_t* c,
                                                                           xcb_window_t root)
{
    return std::make_unique<Private>(owner, selection_P, c, root);
}

std::unique_ptr<selection_owner::Private>
selection_owner::Private::create(selection_owner* owner, char const* selection_P, int screen_P)
{
    return create(owner, selection_P, QX11Info::connection(), QX11Info::appRootWindow(screen_P));
}

std::unique_ptr<selection_owner::Private> selection_owner::Private::create(selection_owner* owner,
                                                                           char const* selection_P,
                                                                           xcb_connection_t* c,
                                                                           xcb_window_t root)
{
    return std::make_unique<Private>(owner, intern_atom(c, selection_P), c, root);
}

selection_owner::selection_owner(xcb_atom_t selection_P, int screen_P)
    : d_ptr(Private::create(this, selection_P, screen_P))
{
}

selection_owner::selection_owner(char const* selection_P, int screen_P)
    : d_ptr(Private::create(this, selection_P, screen_P))
{
}

selection_owner::selection_owner(xcb_atom_t selection, xcb_connection_t* c, xcb_window_t root)
    : d_ptr(Private::create(this, selection, c, root))
{
}

selection_owner::selection_owner(char const* selection, xcb_connection_t* c, xcb_window_t root)
    : d_ptr(Private::create(this, selection, c, root))
{
}

selection_owner::~selection_owner()
{
    if (d_ptr) {
        release();
        if (d_ptr->window != XCB_WINDOW_NONE) {
            xcb_destroy_window(d_ptr->connection,
                               d_ptr->window); // also makes the selection not owned
        }
    }
}

void selection_owner::Private::claimSucceeded()
{
    state = Idle;

    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = root;
    ev.type = Private::manager_atom;
    ev.data.data32[0] = timestamp;
    ev.data.data32[1] = selection;
    ev.data.data32[2] = window;
    ev.data.data32[3] = extra1;
    ev.data.data32[4] = extra2;

    xcb_send_event(connection, false, root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char const*)&ev);
    Q_EMIT owner->claimedOwnership();
}

void selection_owner::Private::gotTimestamp()
{
    Q_ASSERT(state == WaitingForTimestamp);

    state = Idle;

    // Set the selection owner and immediately verify that the claim was successful
    xcb_set_selection_owner(connection, window, selection, timestamp);
    xcb_window_t new_owner = get_selection_owner(connection, selection);

    if (new_owner != window) {
        // qDebug() << "Failed to claim selection : " << new_owner;
        xcb_destroy_window(connection, window);
        timestamp = XCB_CURRENT_TIME;
        window = XCB_NONE;

        Q_EMIT owner->failedToClaimOwnership();
        return;
    }

    if (prev_owner != XCB_NONE && force_kill) {
        // qDebug() << "Waiting for previous owner to disown";
        timer.start(1000, owner);
        state = WaitingForPreviousOwner;

        // Note: We've already selected for structure notify events
        //       on the previous owner window
    } else {
        // If there was no previous owner, we're done
        claimSucceeded();
    }
}

void selection_owner::Private::timeout()
{
    Q_ASSERT(state == WaitingForPreviousOwner);

    state = Idle;

    if (!force_kill) {
        Q_EMIT owner->failedToClaimOwnership();
        return;
    }

    // Ignore any errors from the kill request
    auto err = xcb_request_check(connection, xcb_kill_client_checked(connection, prev_owner));
    free(err);

    claimSucceeded();
}

void selection_owner::claim(bool force_P, bool force_kill_P)
{
    if (!d_ptr) {
        return;
    }
    Q_ASSERT(d_ptr->state == Private::Idle);

    if (Private::manager_atom == XCB_NONE) {
        getAtoms();
    }

    if (d_ptr->timestamp != XCB_CURRENT_TIME) {
        release();
    }

    auto con = d_ptr->connection;
    d_ptr->prev_owner = get_selection_owner(con, d_ptr->selection);

    if (d_ptr->prev_owner != XCB_NONE) {
        if (!force_P) {
            // qDebug() << "Selection already owned, failing";
            Q_EMIT failedToClaimOwnership();
            return;
        }

        // Select structure notify events so get an event when the previous owner
        // destroys the window
        uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xcb_change_window_attributes(con, d_ptr->prev_owner, XCB_CW_EVENT_MASK, &mask);
    }

    uint32_t values[] = {true, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};

    d_ptr->window = xcb_generate_id(con);
    xcb_create_window(con,
                      XCB_COPY_FROM_PARENT,
                      d_ptr->window,
                      d_ptr->root,
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                      values);

    // Trigger a property change event so we get a timestamp
    xcb_atom_t tmp = XCB_ATOM_ATOM;
    xcb_change_property(con,
                        XCB_PROP_MODE_REPLACE,
                        d_ptr->window,
                        XCB_ATOM_ATOM,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        (void const*)&tmp);

    // Now we have to return to the event loop and wait for the property change event
    d_ptr->force_kill = force_kill_P;
    d_ptr->state = Private::WaitingForTimestamp;
}

// destroy resource first
void selection_owner::release()
{
    if (!d_ptr) {
        return;
    }
    if (d_ptr->timestamp == XCB_CURRENT_TIME) {
        return;
    }

    // also makes the selection not owned
    xcb_destroy_window(d_ptr->connection, d_ptr->window);
    d_ptr->window = XCB_NONE;

    // qDebug() << "Releasing selection";

    d_ptr->timestamp = XCB_CURRENT_TIME;
}

xcb_window_t selection_owner::ownerWindow() const
{
    if (!d_ptr) {
        return XCB_WINDOW_NONE;
    }
    if (d_ptr->timestamp == XCB_CURRENT_TIME) {
        return XCB_NONE;
    }

    return d_ptr->window;
}

void selection_owner::setData(uint32_t extra1_P, uint32_t extra2_P)
{
    if (!d_ptr) {
        return;
    }
    d_ptr->extra1 = extra1_P;
    d_ptr->extra2 = extra2_P;
}

bool selection_owner::filterEvent(void* ev_P)
{
    if (!d_ptr) {
        return false;
    }

    auto event = reinterpret_cast<xcb_generic_event_t*>(ev_P);
    uint const response_type = event->response_type & ~0x80;

    switch (response_type) {
    case XCB_SELECTION_CLEAR: {
        xcb_selection_clear_event_t* ev = reinterpret_cast<xcb_selection_clear_event_t*>(event);
        if (d_ptr->timestamp == XCB_CURRENT_TIME || ev->selection != d_ptr->selection) {
            return false;
        }

        d_ptr->timestamp = XCB_CURRENT_TIME;
        //      qDebug() << "Lost selection";

        xcb_window_t window = d_ptr->window;
        Q_EMIT lostOwnership();

        // Unset the event mask before we destroy the window so we don't get a destroy event
        uint32_t event_mask = XCB_NONE;
        xcb_change_window_attributes(d_ptr->connection, window, XCB_CW_EVENT_MASK, &event_mask);
        xcb_destroy_window(d_ptr->connection, window);
        return true;
    }
    case XCB_DESTROY_NOTIFY: {
        xcb_destroy_notify_event_t* ev = reinterpret_cast<xcb_destroy_notify_event_t*>(event);
        if (ev->window == d_ptr->prev_owner) {
            if (d_ptr->state == Private::WaitingForPreviousOwner) {
                d_ptr->timer.stop();
                d_ptr->claimSucceeded();
                return true;
            }
            // It is possible for the previous owner to be destroyed
            // while we're waiting for the timestamp
            d_ptr->prev_owner = XCB_NONE;
        }

        if (d_ptr->timestamp == XCB_CURRENT_TIME || ev->window != d_ptr->window) {
            return false;
        }

        d_ptr->timestamp = XCB_CURRENT_TIME;
        //      qDebug() << "Lost selection (destroyed)";
        Q_EMIT lostOwnership();
        return true;
    }
    case XCB_SELECTION_NOTIFY: {
        auto ev = reinterpret_cast<xcb_selection_notify_event_t*>(event);
        if (d_ptr->timestamp == XCB_CURRENT_TIME || ev->selection != d_ptr->selection) {
            return false;
        }

        // ignore?
        return false;
    }
    case XCB_SELECTION_REQUEST:
        filter_selection_request(event);
        return false;
    case XCB_PROPERTY_NOTIFY: {
        auto ev = reinterpret_cast<xcb_property_notify_event_t*>(event);
        if (ev->window == d_ptr->window && d_ptr->state == Private::WaitingForTimestamp) {
            d_ptr->timestamp = ev->time;
            d_ptr->gotTimestamp();
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

void selection_owner::timerEvent(QTimerEvent* event)
{
    if (!d_ptr) {
        QObject::timerEvent(event);
        return;
    }
    if (event->timerId() == d_ptr->timer.timerId()) {
        d_ptr->timer.stop();
        d_ptr->timeout();
        return;
    }

    QObject::timerEvent(event);
}

void selection_owner::filter_selection_request(void* event)
{
    if (!d_ptr) {
        return;
    }
    xcb_selection_request_event_t* ev = reinterpret_cast<xcb_selection_request_event_t*>(event);

    if (d_ptr->timestamp == XCB_CURRENT_TIME || ev->selection != d_ptr->selection) {
        return;
    }

    if (ev->time != XCB_CURRENT_TIME && ev->time - d_ptr->timestamp > 1U << 31) {
        return; // too old or too new request
    }

    // qDebug() << "Got selection request";

    auto con = d_ptr->connection;
    bool handled = false;

    if (ev->target == Private::xa_multiple) {
        if (ev->property != XCB_NONE) {
            int const MAX_ATOMS = 100;

            auto cookie = xcb_get_property(
                con, false, ev->requestor, ev->property, XCB_GET_PROPERTY_TYPE_ANY, 0, MAX_ATOMS);
            auto reply = xcb_get_property_reply(con, cookie, nullptr);

            if (reply && reply->format == 32 && reply->value_len % 2 == 0) {
                auto atoms = reinterpret_cast<xcb_atom_t*>(xcb_get_property_value(reply));
                bool handled_array[MAX_ATOMS];

                for (uint i = 0; i < reply->value_len / 2; i++) {
                    handled_array[i]
                        = handle_selection(atoms[i * 2], atoms[i * 2 + 1], ev->requestor);
                }

                bool all_handled = true;
                for (uint i = 0; i < reply->value_len / 2; i++) {
                    if (!handled_array[i]) {
                        all_handled = false;
                        atoms[i * 2 + 1] = XCB_NONE;
                    }
                }

                if (!all_handled) {
                    xcb_change_property(con,
                                        ev->requestor,
                                        ev->property,
                                        XCB_ATOM_ATOM,
                                        32,
                                        XCB_PROP_MODE_REPLACE,
                                        reply->value_len,
                                        reinterpret_cast<void const*>(atoms));
                }

                handled = true;
            }

            if (reply) {
                free(reply);
            }
        }
    } else {
        if (ev->property == XCB_NONE) { // obsolete client
            ev->property = ev->target;
        }

        handled = handle_selection(ev->target, ev->property, ev->requestor);
    }

    xcb_selection_notify_event_t xev;
    xev.response_type = XCB_SELECTION_NOTIFY;
    xev.selection = ev->selection;
    xev.requestor = ev->requestor;
    xev.target = ev->target;
    xev.property = handled ? ev->property : XCB_NONE;

    xcb_send_event(con, false, ev->requestor, 0, (char const*)&xev);
}

bool selection_owner::handle_selection(xcb_atom_t target_P,
                                       xcb_atom_t property_P,
                                       xcb_window_t requestor_P)
{
    if (!d_ptr) {
        return false;
    }
    if (target_P == Private::xa_timestamp) {
        // qDebug() << "Handling timestamp request";
        xcb_change_property(d_ptr->connection,
                            requestor_P,
                            property_P,
                            XCB_ATOM_INTEGER,
                            32,
                            XCB_PROP_MODE_REPLACE,
                            1,
                            reinterpret_cast<void const*>(&d_ptr->timestamp));
    } else if (target_P == Private::xa_targets) {
        replyTargets(property_P, requestor_P);
    } else if (genericReply(target_P, property_P, requestor_P)) {
        // handled
    } else {
        // unknown
        return false;
    }

    return true;
}

void selection_owner::replyTargets(xcb_atom_t property_P, xcb_window_t requestor_P)
{
    if (!d_ptr) {
        return;
    }
    xcb_atom_t atoms[3] = {Private::xa_multiple, Private::xa_timestamp, Private::xa_targets};

    xcb_change_property(d_ptr->connection,
                        requestor_P,
                        property_P,
                        XCB_ATOM_ATOM,
                        32,
                        XCB_PROP_MODE_REPLACE,
                        sizeof(atoms) / sizeof(atoms[0]),
                        reinterpret_cast<void const*>(atoms));
}

bool selection_owner::genericReply(xcb_atom_t, xcb_atom_t, xcb_window_t)
{
    return false;
}

void selection_owner::getAtoms()
{
    if (!d_ptr) {
        return;
    }
    if (Private::manager_atom != XCB_NONE) {
        return;
    }

    auto con = d_ptr->connection;

    struct {
        char const* name;
        xcb_atom_t* atom;
    } atoms[] = {{"MANAGER", &Private::manager_atom},
                 {"MULTIPLE", &Private::xa_multiple},
                 {"TARGETS", &Private::xa_targets},
                 {"TIMESTAMP", &Private::xa_timestamp}};

    int const count = sizeof(atoms) / sizeof(atoms[0]);
    xcb_intern_atom_cookie_t cookies[count];

    for (int i = 0; i < count; i++) {
        cookies[i] = xcb_intern_atom(con, false, strlen(atoms[i].name), atoms[i].name);
    }

    for (int i = 0; i < count; i++) {
        if (auto reply = xcb_intern_atom_reply(con, cookies[i], nullptr)) {
            *atoms[i].atom = reply->atom;
            free(reply);
        }
    }
}

xcb_atom_t selection_owner::Private::manager_atom = XCB_NONE;
xcb_atom_t selection_owner::Private::xa_multiple = XCB_NONE;
xcb_atom_t selection_owner::Private::xa_targets = XCB_NONE;
xcb_atom_t selection_owner::Private::xa_timestamp = XCB_NONE;

}
