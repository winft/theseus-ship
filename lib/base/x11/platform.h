/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/logging.h"
#include "base/platform.h"
#include "base/singleton_interface.h"
#include "base/x11/event_filter.h"
#include "base/x11/output.h"
#include "base/x11/output_helpers.h"
#include "base/x11/randr_filter.h"
#include "input/x11/platform.h"
#include "render/x11/platform.h"
#include "script/platform.h"
#include "win/x11/space.h"

#include <memory>
#include <vector>

namespace KWin::base::x11
{

class platform : public base::platform
{
public:
    using output_t = base::x11::output<platform>;
    using render_t = render::x11::platform<platform>;
    using input_t = input::x11::platform<platform>;
    using space_t = win::x11::space<render_t, input_t>;

    platform(base::config config)
        : base::platform(std::move(config))
    {
        operation_mode = operation_mode::x11;

        singleton_interface::platform = this;
        singleton_interface::get_outputs = [this] {
            std::vector<base::output*> vec;
            for (auto&& output : outputs) {
                vec.push_back(output);
            }
            return vec;
        };
    }

    ~platform() override
    {
        for (auto out : outputs) {
            delete out;
        }
        singleton_interface::get_outputs = {};
        singleton_interface::platform = nullptr;
    }

    void update_outputs()
    {
        if (!randr_filter) {
            randr_filter = std::make_unique<base::x11::randr_filter<platform>>(*this);
            update_outputs_impl<base::x11::xcb::randr::screen_resources>();
            return;
        }

        update_outputs_impl<base::x11::xcb::randr::current_resources>();
    }

    std::vector<output_t*> outputs;
    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<scripting::platform<space_t>> script;

    bool is_crash_restart{false};

private:
    template<typename Resources>
    void update_outputs_impl()
    {
        auto res_outs = base::x11::get_outputs_from_resources(
            *this, Resources(x11_data.connection, x11_data.root_window));

        qCDebug(KWIN_CORE) << "Update outputs:" << this->outputs.size() << "-->" << res_outs.size();

        // First check for removed outputs (we go backwards through the outputs, LIFO).
        for (auto old_it = this->outputs.rbegin(); old_it != this->outputs.rend();) {
            auto x11_old_out = static_cast<output_t*>(*old_it);

            auto is_in_new_outputs = [x11_old_out, &res_outs] {
                auto it = std::find_if(
                    res_outs.begin(), res_outs.end(), [x11_old_out](auto const& out) {
                        return x11_old_out->data.crtc == out->data.crtc
                            && x11_old_out->data.name == out->data.name;
                    });
                return it != res_outs.end();
            };

            if (is_in_new_outputs()) {
                // The old output is still there. Keep it in the base outputs.
                old_it++;
                continue;
            }

            qCDebug(KWIN_CORE) << "  removed:" << x11_old_out->name();
            auto old_out = *old_it;
            old_it = static_cast<decltype(old_it)>(this->outputs.erase(std::next(old_it).base()));
            Q_EMIT output_removed(old_out);
            delete old_out;
        }

        // Second check for added outputs.
        for (auto& out : res_outs) {
            auto it = std::find_if(
                this->outputs.begin(), this->outputs.end(), [&out](auto const& old_out) {
                    auto old_x11_out = static_cast<output_t*>(old_out);
                    return old_x11_out->data.crtc == out->data.crtc
                        && old_x11_out->data.name == out->data.name;
                });
            if (it == this->outputs.end()) {
                qCDebug(KWIN_CORE) << "  added:" << out->name();
                this->outputs.push_back(out.release());
                Q_EMIT output_added(this->outputs.back());
            }
        }

        update_output_topology(*this);
    }

    std::unique_ptr<base::x11::event_filter> randr_filter;
};

}
