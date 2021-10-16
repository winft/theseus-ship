/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "workspace.h"

#include <kwin_export.h>
#include <memory>

namespace KWin::win::wayland
{
struct xdg_activation;

class KWIN_EXPORT space : public Workspace
{
    Q_OBJECT
public:
    space();
    ~space() override;

    std::unique_ptr<win::wayland::xdg_activation> activation;
};

}
