/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "backend.h"

#include "buffer.h"
#include "egl_backend.h"
#include "egl_output.h"
#include "output.h"
#include "wlr_helpers.h"

#include "base/platform.h"
#include "base/wayland/output_helpers.h"
#include "input/wayland/platform.h"
#include "main.h"
#include "render/wayland/compositor.h"
#include "render/wayland/effects.h"
#include "screens.h"
#include "wayland_server.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

static auto align_horizontal{false};

backend::backend(base::platform<base::backend::wlroots, AbstractWaylandOutput>& base)
    : base{base}
{
    align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

    setSupportsGammaControl(true);
}

backend::~backend()
{
    for (auto output : all_outputs) {
        // Outputs are currently deleted through Qt parent-child relation.
        output->back = nullptr;
        delete output;
    }
    all_outputs.clear();
    base.all_outputs.clear();
}

void handle_new_output(struct wl_listener* listener, void* data)
{
    base::event_receiver<backend>* new_output_struct
        = wl_container_of(listener, new_output_struct, event);
    auto back = new_output_struct->receiver;
    auto wlr_out = reinterpret_cast<wlr_output*>(data);

    if (!wl_list_empty(&wlr_out->modes)) {
        auto mode = wlr_output_preferred_mode(wlr_out);
        wlr_output_set_mode(wlr_out, mode);
        wlr_output_enable(wlr_out, true);
        if (!wlr_output_test(wlr_out)) {
            return;
        }
        if (!wlr_output_commit(wlr_out)) {
            return;
        }
    }

    auto const screens_width = std::max(screens()->size().width(), 0);

    auto out = new output(wlr_out, back);
    back->all_outputs << out;
    back->base.all_outputs.push_back(out);
    back->enabled_outputs << out;
    back->base.enabled_outputs.push_back(out);

    if (align_horizontal) {
        auto shifted_geo = out->geometry();
        shifted_geo.moveLeft(screens_width);
        out->force_geometry(shifted_geo);
    }

    Q_EMIT back->output_added(out);
    Screens::self()->updateAll();
}

void backend::init()
{
    // TODO(romangg): Can we omit making a distinction here?
    // Pointer warping is required for tests.
    setSupportsPointerWarping(base::backend::wlroots_get_headless_backend(base.backend.backend));

    assert(base.backend.backend);
    fd = wlr_backend_get_drm_fd(base.backend.backend);

    new_output.receiver = this;
    new_output.event.notify = handle_new_output;
    wl_signal_add(&base.backend.backend->events.new_output, &new_output.event);

    init_drm_leasing();

    if (!wlr_backend_start(base.backend.backend)) {
        throw std::exception();
    }

    Screens::self()->updateAll();
}

Outputs backend::outputs() const
{
    return all_outputs;
}

Outputs backend::enabledOutputs() const
{
    return enabled_outputs;
}

void backend::enableOutput(output* output, bool enable)
{
    if (enable) {
        Q_ASSERT(!enabled_outputs.contains(output));
        enabled_outputs << output;
        base.enabled_outputs.push_back(output);
        Q_EMIT output_added(output);
    } else {
        Q_ASSERT(enabled_outputs.contains(output));
        enabled_outputs.removeOne(output);
        Q_ASSERT(!enabled_outputs.contains(output));
        remove_all(base.enabled_outputs, output);
        Q_EMIT output_removed(output);
    }

    auto const& wlroots_base = static_cast<ApplicationWaylandAbstract*>(kwinApp())->get_base();
    auto wayland_input = static_cast<input::wayland::platform*>(kwinApp()->input.get());
    base::wayland::check_outputs_on(wlroots_base, wayland_input->dpms_filter);

    Screens::self()->updateAll();
}

clockid_t backend::clockId() const
{
    return wlr_backend_get_presentation_clock(base.backend.backend);
}

OpenGLBackend* backend::createOpenGLBackend()
{
    egl = new egl_backend(this, base::backend::wlroots_get_headless_backend(base.backend.backend));
    return egl;
}

void backend::createEffectsHandler(render::compositor* compositor, Scene* scene)
{
    new wayland::effects_handler_impl(compositor, scene);
}

QVector<CompositingType> backend::supportedCompositors() const
{
    if (selectedCompositor() != NoCompositing) {
        return {selectedCompositor()};
    }
    return QVector<CompositingType>{OpenGLCompositing};
}

QString backend::supportInformation() const
{
    QString supportInfo;
    QDebug s(&supportInfo);
    s.nospace();
    s << "Name: "
      << "wlroots" << endl;
    return supportInfo;
}

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

void backend::init_drm_leasing()
{
#if HAVE_WLR_DRM_LEASE
    auto drm_backend = base::backend::wlroots_get_drm_backend(base.backend.backend);
    if (!drm_backend) {
        return;
    }

    auto server = waylandServer();
    server->createDrmLeaseDevice();

    connect(server->drm_lease_device(),
            &Wrapland::Server::drm_lease_device_v1::needs_new_client_fd,
            this,
            [device = server->drm_lease_device(), drm_backend] {
                // TODO(romangg): wait in case not DRM master at the moment.
                auto fd = wlr_drm_backend_get_non_master_fd(drm_backend);
                device->update_fd(fd);
            });
    connect(server->drm_lease_device(),
            &Wrapland::Server::drm_lease_device_v1::leased,
            this,
            [this](auto lease) {
                try {
                    process_drm_leased(lease);
                } catch (...) {
                    qCWarning(KWIN_WL) << "Creating lease failed.";
                    lease->finish();
                }
            });
#endif
}

void backend::process_drm_leased([[maybe_unused]] Wrapland::Server::drm_lease_v1* lease)
{
#if HAVE_WLR_DRM_LEASE
    std::vector<output*> outputs;

    qCDebug(KWIN_WL) << "Client tries to lease DRM resources.";

    if (lease->connectors().empty()) {
        qCDebug(KWIN_WL) << "Lease request has no connectors specified.";
        throw;
    }

    auto const& wlroots_base = static_cast<ApplicationWaylandAbstract*>(kwinApp())->get_base();

    for (auto& con : lease->connectors()) {
        auto out = static_cast<output*>(base::wayland::find_output(wlroots_base, con->output()));
        assert(out);
        outputs.push_back(out);
    }

    auto outputs_array = outputs_array_wrap(outputs.size());

    size_t i{0};
    for (auto& out : outputs) {
        egl->get_output(out).cleanup_framebuffer();
        outputs_array.data[i] = out->native;
        i++;
    }

    uint32_t lessee_id;
    auto fd = wlr_drm_create_lease(outputs_array.data, outputs_array.size, &lessee_id);
    if (fd < 0) {
        qCWarning(KWIN_WL) << "Error in wlroots backend on lease creation.";
        for (auto& out : outputs) {
            egl->get_output(out).reset_framebuffer();
        }
        throw;
    }

    connect(lease, &Wrapland::Server::drm_lease_v1::resourceDestroyed, this, [this, lessee_id] {
        wlr_drm_backend_terminate_lease(
            base::backend::wlroots_get_drm_backend(base.backend.backend), lessee_id);
        static_cast<render::wayland::compositor*>(compositor::self())->unlock();
    });

    static_cast<render::wayland::compositor*>(compositor::self())->lock();
    lease->grant(fd);
    qCDebug(KWIN_WL) << "DRM resources have been leased to client";
#endif
}

void backend::setVirtualOutputs(int count, QVector<QRect> geometries, QVector<int> scales)
{
    assert(geometries.size() == 0 || geometries.size() == count);
    assert(scales.size() == 0 || scales.size() == count);

    auto outputs_copy = all_outputs;
    for (auto output : outputs_copy) {
        delete output;
    }

    auto sum_width = 0;
    for (int i = 0; i < count; i++) {
        auto const scale = scales.size() ? scales.at(i) : 1.;
        auto const size
            = (geometries.size() ? geometries.at(i).size() : initialWindowSize()) * scale;

        wlr_headless_add_output(base.backend.backend, size.width(), size.height());

        auto added_output = all_outputs.back();

        if (geometries.size()) {
            added_output->force_geometry(geometries.at(i));
        } else {
            auto const geo = QRect(QPoint(sum_width, 0), initialWindowSize() * scale);
            added_output->force_geometry(geo);
            sum_width += geo.width();
        }
    }

    // Update again in case of force geometry change.
    Screens::self()->updateAll();
}

}
