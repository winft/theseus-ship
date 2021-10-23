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

    QRect get_icon_geometry(Toplevel const* win) const override;

    std::unique_ptr<win::wayland::xdg_activation> activation;

protected:
    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas) override;
};

}
