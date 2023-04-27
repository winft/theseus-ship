/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/context_attribute_builder.h"

namespace KWin::render::backend::x11
{

class glx_context_attribute_builder : public gl::context_attribute_builder
{
public:
    std::vector<int> build() const override;
};

}
