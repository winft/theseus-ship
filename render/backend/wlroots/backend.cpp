/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "backend.h"

#include "buffer.h"
#include "egl_backend.h"
#include "output.h"
#include "wlr_helpers.h"

#include "main.h"
#include "platform/wlroots.h"
#include "screens.h"
#include "wayland_server.h"
#include <render/compositor.h>

#include <QDebug>

namespace KWin::render::backend::wlroots
{

static auto align_horizontal{false};

backend::backend(platform_base::wlroots* base, QObject* parent)
    : Platform(parent)
    , base{base}
{
    align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

    setSupportsGammaControl(true);
    supportsOutputChanges();
}

backend::~backend()
{
    for (auto output : all_outputs) {
        // Outputs are currently deleted through Qt parent-child relation.
        output->back = nullptr;
        delete output;
    }
    all_outputs.clear();
}

void handle_new_output(struct wl_listener* listener, void* data)
{
    event_receiver<backend>* new_output_struct
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
    back->enabled_outputs << out;

    if (align_horizontal) {
        auto shifted_geo = out->geometry();
        shifted_geo.moveLeft(screens_width);
        out->forceGeometry(shifted_geo);
    }

    Q_EMIT back->output_added(out);
    Screens::self()->updateAll();
}

void backend::init()
{
    // TODO(romangg): Can we omit making a distinction here?
    // Pointer warping is required for tests.
    setSupportsPointerWarping(platform_base::wlroots_get_headless_backend(base->backend));

    assert(base->backend);
    fd = wlr_backend_get_drm_fd(base->backend);

    new_output.receiver = this;
    new_output.event.notify = handle_new_output;
    wl_signal_add(&base->backend->events.new_output, &new_output.event);

    if (!wlr_backend_start(base->backend)) {
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
        Q_EMIT output_added(output);
    } else {
        Q_ASSERT(enabled_outputs.contains(output));
        enabled_outputs.removeOne(output);
        Q_ASSERT(!enabled_outputs.contains(output));
        Q_EMIT output_removed(output);
    }
    checkOutputsOn();

    Screens::self()->updateAll();
}

clockid_t backend::clockId() const
{
    return wlr_backend_get_presentation_clock(base->backend);
}

OpenGLBackend* backend::createOpenGLBackend()
{
    egl = new egl_backend(this, platform_base::wlroots_get_headless_backend(base->backend));
    return egl;
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

        wlr_headless_add_output(base->backend, size.width(), size.height());

        auto added_output = all_outputs.back();

        if (geometries.size()) {
            added_output->forceGeometry(geometries.at(i));
        } else {
            auto const geo = QRect(QPoint(sum_width, 0), initialWindowSize() * scale);
            added_output->forceGeometry(geo);
            sum_width += geo.width();
        }
    }

    // Update again in case of force geometry change.
    Screens::self()->updateAll();
}

}
