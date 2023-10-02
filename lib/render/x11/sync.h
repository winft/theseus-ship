/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/x11/data.h>

#include <xcb/sync.h>
#include <xcb/xcb.h>

namespace KWin::render::x11
{

/**
 * Represents a fence used to synchronize operations in the kwin command stream with operations in
 * the X command stream.
 */
class sync_object
{
public:
    enum State {
        Ready,
        TriggerSent,
        Waiting,
        Done,
        Resetting,
    };

    sync_object() = default;

    sync_object(xcb_connection_t* con, xcb_window_t root_window)
        : con{con}
    {
        m_state = Ready;

        m_fence = xcb_generate_id(con);
        xcb_sync_create_fence(con, root_window, m_fence, false);
        xcb_flush(con);

        m_sync = glImportSyncEXT(GL_SYNC_X11_FENCE_EXT, m_fence, 0);
        m_reset_cookie.sequence = 0;
    }

    ~sync_object()
    {
        // If glDeleteSync is called before the xcb fence is signalled
        // the nvidia driver (the only one to implement GL_SYNC_X11_FENCE_EXT)
        // deadlocks waiting for the fence to be signalled.
        // To avoid this, make sure the fence is signalled before
        // deleting the sync.
        if (m_state == Resetting || m_state == Ready) {
            trigger();
            // The flush is necessary!
            // The trigger command needs to be sent to the X server.
            xcb_flush(con);
        }
        xcb_sync_destroy_fence(con, m_fence);
        glDeleteSync(m_sync);

        if (m_state == Resetting)
            xcb_discard_reply(con, m_reset_cookie.sequence);
    }

    State state() const
    {
        return m_state;
    }

    void trigger()
    {
        Q_ASSERT(m_state == Ready || m_state == Resetting);

        // Finish resetting the fence if necessary
        if (m_state == Resetting)
            finishResetting();

        xcb_sync_trigger_fence(con, m_fence);
        m_state = TriggerSent;
    }

    void wait()
    {
        if (m_state != TriggerSent)
            return;

        glWaitSync(m_sync, 0, GL_TIMEOUT_IGNORED);
        m_state = Waiting;
    }

    bool finish()
    {
        if (m_state == Done)
            return true;

        // Note: It is possible that we never inserted a wait for the fence.
        //       This can happen if we ended up not rendering the damaged
        //       window because it is fully occluded.
        Q_ASSERT(m_state == TriggerSent || m_state == Waiting);

        // Check if the fence is signaled
        GLint value;
        glGetSynciv(m_sync, GL_SYNC_STATUS, 1, nullptr, &value);

        if (value != GL_SIGNALED) {
            qCDebug(KWIN_CORE) << "Waiting for X fence to finish";

            // Wait for the fence to become signaled with a one second timeout
            const GLenum result = glClientWaitSync(m_sync, 0, 1000000000);

            switch (result) {
            case GL_TIMEOUT_EXPIRED:
                qCWarning(KWIN_CORE) << "Timeout while waiting for X fence";
                return false;

            case GL_WAIT_FAILED:
                qCWarning(KWIN_CORE) << "glClientWaitSync() failed";
                return false;
            }
        }

        m_state = Done;
        return true;
    }

    void reset()
    {
        Q_ASSERT(m_state == Done);

        // Send the reset request along with a sync request.
        // We use the cookie to ensure that the server has processed the reset
        // request before we trigger the fence and call glWaitSync().
        // Otherwise there is a race condition between the reset finishing and
        // the glWaitSync() call.
        xcb_sync_reset_fence(con, m_fence);
        m_reset_cookie = xcb_get_input_focus(con);
        xcb_flush(con);

        m_state = Resetting;
    }

    void finishResetting()
    {
        Q_ASSERT(m_state == Resetting);
        free(xcb_get_input_focus_reply(con, m_reset_cookie, nullptr));
        m_state = Ready;
    }

private:
    State m_state;
    GLsync m_sync;
    xcb_sync_fence_t m_fence;
    xcb_get_input_focus_cookie_t m_reset_cookie;
    xcb_connection_t* con;
};

/// Manages a set of fences used for explicit synchronization with the X command stream.
class sync_manager
{
public:
    enum { MaxFences = 4 };

    sync_manager(base::x11::data const& data)
    {
        m_fences.fill(sync_object(data.connection, data.root_window));
    }

    void trigger()
    {
        current_fence = nextFence();
        current_fence->trigger();
    }

    void wait()
    {
        if (current_fence && current_fence->state() != x11::sync_object::Waiting) {
            current_fence->wait();
        }
    }

    bool updateFences()
    {
        for (int i = 0; i < qMin(2, MaxFences - 1); i++) {
            const int index = (m_next + i) % MaxFences;
            sync_object& fence = m_fences[index];

            switch (fence.state()) {
            case sync_object::Ready:
                break;

            case sync_object::TriggerSent:
            case sync_object::Waiting:
                if (!fence.finish()) {
                    qCDebug(KWIN_CORE)
                        << "Error on explicit synchronization with the X command stream.";
                    return false;
                }
                fence.reset();
                break;

                // Should not happen in practice since we always reset the fence
                // after finishing it
            case sync_object::Done:
                fence.reset();
                break;

            case sync_object::Resetting:
                fence.finishResetting();
                break;
            }
        }

        return true;
    }

private:
    sync_object* nextFence()
    {
        sync_object* fence = &m_fences[m_next];
        m_next = (m_next + 1) % MaxFences;
        return fence;
    }

    std::array<sync_object, MaxFences> m_fences;
    int m_next{0};
    sync_object* current_fence{nullptr};
};

}
