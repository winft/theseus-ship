/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output_transform.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "input/filters/dpms.h"
#include "input/wayland/dpms.h"
#include "wayland_logging.h"

#include <Wrapland/Server/output.h>
#include <Wrapland/Server/wlr_output_configuration_head_v1.h>
#include <Wrapland/Server/wlr_output_configuration_v1.h>
#include <algorithm>

namespace KWin::base::wayland
{

template<typename Base>
auto find_output(Base const& base, Wrapland::Server::output const* output) ->
    typename Base::output_t*
{
    auto const& outs = base.all_outputs;
    auto it = std::find_if(outs.cbegin(), outs.cend(), [output](auto out) {
        return out->wrapland_output() == output;
    });
    if (it != outs.cend()) {
        return *it;
    }
    return nullptr;
}

template<typename Base>
void request_outputs_change(Base& base, Wrapland::Server::wlr_output_configuration_v1& config)
{
    auto config_heads = config.enabled_heads();

    for (auto&& output : base.all_outputs) {
        auto it = std::find_if(config_heads.begin(), config_heads.end(), [&](auto head) {
            return output->wrapland_output() == &head->get_output();
        });
        if (it == config_heads.end()) {
            output->set_enabled(false);
            Q_EMIT output->qobject->mode_changed();
            continue;
        }

        output->apply_changes((*it)->get_state());
    }

    config.send_succeeded();
    base.server->output_manager->commit_changes();
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

template<typename Base>
void check_outputs_on(Base const& base)
{
    if (!base.space || !base.space->input || !base.space->input->dpms_filter) {
        // No DPMS filter exists, all outputs are on.
        return;
    }

    auto const& outs = base.outputs;
    if (std::all_of(outs.cbegin(), outs.cend(), [](auto&& out) { return out->is_dpms_on(); })) {
        // All outputs are on, disable the filter.
        base.space->input->dpms_filter.reset();
    }
}

inline Wrapland::Server::output_dpms_mode to_wayland_dpms_mode(base::dpms_mode mode)
{
    switch (mode) {
    case base::dpms_mode::on:
        return Wrapland::Server::output_dpms_mode::on;
    case base::dpms_mode::standby:
        return Wrapland::Server::output_dpms_mode::standby;
    case base::dpms_mode::suspend:
        return Wrapland::Server::output_dpms_mode::suspend;
    case base::dpms_mode::off:
        return Wrapland::Server::output_dpms_mode::off;
    default:
        Q_UNREACHABLE();
    }
}

template<typename Base>
void output_set_dpms_on(typename Base::output_t& output, Base& base)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << output.name() << "to On.";
    output.m_dpms = base::dpms_mode::on;

    if (output.is_enabled()) {
        output.m_output->set_dpms_mode(Wrapland::Server::output_dpms_mode::on);
    }

    check_outputs_on(base);
}

template<typename Base>
void output_set_dmps_off(base::dpms_mode mode, typename Base::output_t& output, Base& base)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << output.name() << "to Off.";

    if (!base.space || !base.space->input) {
        qCWarning(KWIN_WL) << "Abort setting DPMS. Can't create filter to set DPMS to on again.";
        return;
    }

    output.m_dpms = mode;

    if (output.is_enabled()) {
        output.m_output->set_dpms_mode(to_wayland_dpms_mode(mode));
        input::wayland::create_dpms_filter(*base.space->input);
    }
}

}
