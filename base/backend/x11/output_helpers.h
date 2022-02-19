/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/output.h"
#include "base/x11/xcb/randr.h"

#include <memory>
#include <vector>
#include <xcb/randr.h>

namespace KWin::base::x11
{

template<typename Base, typename Resources>
std::vector<std::unique_ptr<base::x11::output>> get_outputs_from_resources(Base& base,
                                                                           Resources resources)
{
    auto add_fallback = [&base](auto& outputs) {
        auto output = std::make_unique<base::x11::output>(base);
        output->data.gamma_ramp_size = 0;
        output->data.refresh_rate = -1.0f;
        output->data.name = QStringLiteral("Fallback");
        outputs.push_back(std::move(output));
    };

    std::vector<std::unique_ptr<base::x11::output>> outputs;

    if (resources.is_null()) {
        add_fallback(outputs);
        return outputs;
    }

    xcb_randr_crtc_t* crtcs = resources.crtcs();
    xcb_randr_mode_info_t* modes = resources.modes();

    std::vector<base::x11::xcb::randr::crtc_info> crtc_infos(resources->num_crtcs);
    for (int i = 0; i < resources->num_crtcs; ++i) {
        crtc_infos[i] = base::x11::xcb::randr::crtc_info(crtcs[i], resources->config_timestamp);
    }

    for (int i = 0; i < resources->num_crtcs; ++i) {
        base::x11::xcb::randr::crtc_info crtc_info(crtc_infos.at(i));

        auto randr_outputs = crtc_info.outputs();
        std::vector<base::x11::xcb::randr::output_info> output_infos(
            randr_outputs ? resources->num_outputs : 0);

        if (randr_outputs) {
            for (int i = 0; i < resources->num_outputs; ++i) {
                output_infos[i] = base::x11::xcb::randr::output_info(randr_outputs[i],
                                                                     resources->config_timestamp);
            }
        }

        float refresh_rate = -1.0f;
        for (int j = 0; j < resources->num_modes; ++j) {
            if (crtc_info->mode == modes[j].id) {
                if (modes[j].htotal != 0 && modes[j].vtotal != 0) {
                    // BUG 313996, refresh rate calculation
                    int dotclock = modes[j].dot_clock, vtotal = modes[j].vtotal;
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) {
                        dotclock *= 2;
                    }
                    if (modes[j].mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) {
                        vtotal *= 2;
                    }
                    refresh_rate = dotclock / float(modes[j].htotal * vtotal);
                }
                // Found mode.
                break;
            }
        }

        auto const geo = crtc_info.rect();
        if (geo.isValid()) {
            xcb_randr_crtc_t crtc = crtcs[i];

            // TODO: Perhaps the output has to save the inherited gamma ramp and
            // restore it during tear down. Currently neither standalone x11 nor
            // drm platform do this.
            base::x11::xcb::randr::crtc_gamma gamma(crtc);

            auto output = std::make_unique<base::x11::output>(base);
            output->data.crtc = crtc;
            output->data.gamma_ramp_size = gamma.is_null() ? 0 : gamma->size;
            output->data.geometry = geo;
            output->data.refresh_rate = refresh_rate * 1000;

            for (int j = 0; j < crtc_info->num_outputs; ++j) {
                base::x11::xcb::randr::output_info output_info(output_infos.at(j));
                if (output_info->crtc != crtc) {
                    continue;
                }

                auto physical_size = QSize(output_info->mm_width, output_info->mm_height);

                switch (crtc_info->rotation) {
                case XCB_RANDR_ROTATION_ROTATE_0:
                case XCB_RANDR_ROTATION_ROTATE_180:
                    break;
                case XCB_RANDR_ROTATION_ROTATE_90:
                case XCB_RANDR_ROTATION_ROTATE_270:
                    physical_size.transpose();
                    break;
                case XCB_RANDR_ROTATION_REFLECT_X:
                case XCB_RANDR_ROTATION_REFLECT_Y:
                    break;
                }

                output->data.name = output_info.name();
                output->data.physical_size = physical_size;
                break;
            }

            outputs.push_back(std::move(output));
        }
    }

    if (outputs.empty()) {
        add_fallback(outputs);
    }

    return outputs;
}

}
