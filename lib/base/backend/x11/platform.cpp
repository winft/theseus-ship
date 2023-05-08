/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "output_helpers.h"

#include "base/logging.h"
#include "base/output_helpers.h"
#include "base/x11/randr_filter.h"
#include "base/x11/xcb/randr.h"

namespace KWin::base::backend::x11
{

platform::platform(base::config config)
    : base::x11::platform(std::move(config))
{
    operation_mode = operation_mode::x11;
}

void platform::update_outputs()
{
    if (!randr_filter) {
        randr_filter = std::make_unique<base::x11::randr_filter<platform>>(*this);
        update_outputs_impl<base::x11::xcb::randr::screen_resources>();
        return;
    }

    update_outputs_impl<base::x11::xcb::randr::current_resources>();
}

template<typename Resources>
void platform::update_outputs_impl()
{
    auto res_outs
        = get_outputs_from_resources(*this, Resources(x11_data.connection, x11_data.root_window));

    qCDebug(KWIN_CORE) << "Update outputs:" << this->outputs.size() << "-->" << res_outs.size();

    // First check for removed outputs (we go backwards through the outputs, LIFO).
    for (auto old_it = this->outputs.rbegin(); old_it != this->outputs.rend();) {
        auto x11_old_out = static_cast<base::x11::output*>(*old_it);

        auto is_in_new_outputs = [x11_old_out, &res_outs] {
            auto it
                = std::find_if(res_outs.begin(), res_outs.end(), [x11_old_out](auto const& out) {
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
        auto it
            = std::find_if(this->outputs.begin(), this->outputs.end(), [&out](auto const& old_out) {
                  auto old_x11_out = static_cast<base::x11::output*>(old_out);
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

}
