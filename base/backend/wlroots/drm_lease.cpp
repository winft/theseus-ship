/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drm_lease.h"

#include "non_desktop_output.h"

#include <stdexcept>

namespace KWin::base::backend::wlroots
{

struct outputs_array_wrap {
    outputs_array_wrap(size_t size)
        : size{size}
    {
        data = new wlr_output*[size];
    }
    ~outputs_array_wrap()
    {
        delete[] data;
    }
    wlr_output** data{nullptr};
    size_t size;
};

static void handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    event_receiver<drm_lease>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto lease = event_receiver_struct->receiver;

    lease->wlr_lease = nullptr;
    Q_EMIT lease->finished();
}

drm_lease::drm_lease(Wrapland::Server::drm_lease_v1* lease,
                     std::vector<non_desktop_output*> const& outputs)
    : lease{lease}
    , outputs{outputs}
    , destroyed{std::make_unique<event_receiver<drm_lease>>()}
{
    auto outputs_array = outputs_array_wrap(outputs.size());

    size_t i{0};
    for (auto& output : outputs) {
        outputs_array.data[i] = output->native;
        i++;
    }

    int lease_fd{-1};
    wlr_lease = wlr_drm_create_lease(outputs_array.data, outputs_array.size, &lease_fd);
    if (!wlr_lease) {
        throw std::runtime_error("Error on wlr_drm_create_lease");
    }

    destroyed->receiver = this;
    destroyed->event.notify = handle_destroy;
    wl_signal_add(&wlr_lease->events.destroy, &destroyed->event);

    QObject::connect(lease, &Wrapland::Server::drm_lease_v1::resourceDestroyed, this, [this] {
        this->lease = nullptr;
        if (wlr_lease) {
            auto tmp = wlr_lease;
            wlr_lease = nullptr;
            wlr_drm_lease_terminate(tmp);
        }
    });

    for (auto& output : outputs) {
        output->lease = this;
    }

    lease->grant(lease_fd);
}

drm_lease::drm_lease(drm_lease&& other) noexcept
{
    *this = std::move(other);
}

drm_lease& drm_lease::operator=(drm_lease&& other) noexcept
{
    wlr_lease = other.wlr_lease;
    destroyed = std::move(other.destroyed);
    destroyed->receiver = this;
    other.wlr_lease = nullptr;
    return *this;
}

drm_lease::~drm_lease()
{
    if (lease) {
        lease->finish();
    }
    for (auto output : outputs) {
        output->lease = nullptr;
    }
    if (wlr_lease) {
        wlr_drm_lease_terminate(wlr_lease);
    }
}
}
