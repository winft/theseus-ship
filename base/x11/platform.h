/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "base/platform.h"
#include "base/singleton_interface.h"
#include "input/x11/platform.h"
#include "render/x11/platform.h"
#include "win/x11/space.h"

#include <memory>
#include <vector>

namespace KWin::base::x11
{

class platform : public base::platform
{
public:
    using output_t = output;
    using render_t = render::x11::platform<platform>;
    using input_t = input::x11::platform<platform>;
    using space_t = win::x11::space<platform>;

    platform(base::config config)
        : base::platform(std::move(config))
    {
        singleton_interface::platform = this;
    }

    ~platform() override
    {
        for (auto out : outputs) {
            delete out;
        }
        singleton_interface::platform = nullptr;
    }

    std::vector<base::output*> get_outputs() const override
    {
        std::vector<base::output*> vec;
        for (auto&& output : outputs) {
            vec.push_back(output);
        }
        return vec;
    }

    std::vector<output*> outputs;
    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;

    bool is_crash_restart{false};
};

}
