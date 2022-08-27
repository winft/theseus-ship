/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/output_helpers.h"
#include "input/filters/dpms.h"
#include "input/wayland/dpms.h"
#include "wayland_logging.h"

#include <Wrapland/Server/output_changeset_v1.h>
#include <Wrapland/Server/output_configuration_v1.h>
#include <algorithm>

namespace KWin::base::wayland
{

template<typename Base>
base::wayland::output* find_output(Base const& base, Wrapland::Server::Output const* output)
{
    auto const& outs = base.all_outputs;
    auto it = std::find_if(outs.cbegin(), outs.cend(), [output](auto out) {
        auto wayland_output = dynamic_cast<base::wayland::output*>(out);
        return wayland_output->wrapland_output() == output;
    });
    if (it != outs.cend()) {
        return dynamic_cast<base::wayland::output*>(*it);
    }
    return nullptr;
}

template<typename Base>
void request_outputs_change(Base& base, Wrapland::Server::OutputConfigurationV1* config)
{
    auto const& changes = config->changes();

    for (auto it = changes.begin(); it != changes.end(); it++) {
        auto const changeset = it.value();

        auto output = find_output(base, it.key()->output());
        if (!output) {
            qCWarning(KWIN_WL) << "Could NOT find output:"
                               << it.key()->output()->description().c_str();
            continue;
        }

        output->apply_changes(changeset);
    }

    config->setApplied();
    update_output_topology(base);
}

template<typename Base, typename Filter>
void turn_outputs_on(Base const& base, Filter& filter)
{
    filter.reset();

    for (auto& out : base.outputs) {
        out->update_dpms(base::dpms_mode::on);
    }
}

template<typename Base, typename Filter>
void check_outputs_on(Base const& base, Filter& filter)
{
    if (!filter) {
        // Already disabled, all outputs are on.
        return;
    }

    auto const& outs = base.outputs;
    if (std::all_of(outs.cbegin(), outs.cend(), [](auto&& out) { return out->is_dpms_on(); })) {
        // All outputs are on, disable the filter.
        filter.reset();
    }
}

inline Wrapland::Server::Output::DpmsMode to_wayland_dpms_mode(base::dpms_mode mode)
{
    switch (mode) {
    case base::dpms_mode::on:
        return Wrapland::Server::Output::DpmsMode::On;
    case base::dpms_mode::standby:
        return Wrapland::Server::Output::DpmsMode::Standby;
    case base::dpms_mode::suspend:
        return Wrapland::Server::Output::DpmsMode::Suspend;
    case base::dpms_mode::off:
        return Wrapland::Server::Output::DpmsMode::Off;
    default:
        Q_UNREACHABLE();
    }
}

template<typename Base>
void output_set_dpms_on(base::wayland::output& output, Base& base)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << output.name() << "to On.";
    output.m_dpms = base::dpms_mode::on;

    if (output.is_enabled()) {
        output.m_output->set_dpms_mode(Wrapland::Server::Output::DpmsMode::On);
    }

    check_outputs_on(base, base.input->dpms_filter);
}

template<typename Base>
void output_set_dmps_off(base::dpms_mode mode, base::wayland::output& output, Base& base)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << output.name() << "to Off.";

    output.m_dpms = mode;

    if (output.is_enabled()) {
        output.m_output->set_dpms_mode(to_wayland_dpms_mode(mode));
        input::wayland::create_dpms_filter(base.input.get());
    }
}

}
