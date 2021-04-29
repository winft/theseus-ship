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
#include "screens.h"
#include "wayland_server.h"

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
    back->updateOutputsOn();
    Screens::self()->updateAll();
}

void backend::init()
{
    setSoftWareCursor(true);

    assert(base->backend);
    fd = wlr_backend_get_drm_fd(base->backend);

    new_output.receiver = this;
    new_output.event.notify = handle_new_output;
    wl_signal_add(&base->backend->events.new_output, &new_output.event);

    wlr_backend_start(base->backend);

    Screens::self()->updateAll();
    kwinApp()->continueStartupWithCompositor();
}

void backend::prepareShutdown()
{
    Platform::prepareShutdown();
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
    updateOutputsOn();
    checkOutputsOn();

    Screens::self()->updateAll();
}

void backend::doShowCursor()
{
    // TODO
}

void backend::doHideCursor()
{
    // TODO
}

bool backend::supportsClockId() const
{
    return false;
}

// TODO: Should we return the backend's clock id?

OpenGLBackend* backend::createOpenGLBackend()
{
    egl = new egl_backend(this, is_headless_backend(base->backend));
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

}
