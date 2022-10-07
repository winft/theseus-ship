/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "base/x11/xcb/window.h"
#include "input/cursor.h"
#include "render/window.h"
#include "win/activation.h"
#include "win/control.h"
#include "win/damage.h"
#include "win/remnant.h"
#include "win/rules.h"
#include "win/rules/ruling.h"
#include "win/rules/update.h"
#include "win/shortcut_set.h"
#include "win/window_geometry.h"
#include "win/window_metadata.h"
#include "win/window_qobject.h"
#include "win/window_render_data.h"
#include "win/window_topology.h"

#include <NETWM>
#include <QMatrix4x4>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>

namespace KWin
{

namespace win::x11
{
class client_machine;
}

template<typename Space>
class Toplevel
{
public:
    constexpr static bool is_toplevel{true};

    using space_t = Space;
    using type = Toplevel<space_t>;
    using qobject_t = win::window_qobject;
    using output_t = typename space_t::base_t::output_t;

    std::unique_ptr<qobject_t> qobject;

    win::window_metadata meta;
    win::window_geometry geo;
    win::window_topology<output_t> topo;
    win::window_render_data<output_t> render_data;

    struct {
        QMetaObject::Connection frame_update_outputs;
        QMetaObject::Connection screens_update_outputs;
        QMetaObject::Connection check_screen;
    } notifiers;

    bool is_shape{false};

    Space& space;

    virtual ~Toplevel()
    {
    }

    Wrapland::Server::Surface* surface{nullptr};
    quint32 surface_id{0};

    bool is_outline{false};

    mutable bool is_render_shape_valid{false};

    bool skip_close_animation{false};

    Toplevel(win::remnant remnant, Space& space)
        : type(space)
    {
        this->remnant = std::move(remnant);
    }

    Toplevel(Space& space)
        : meta{++space.window_id}
        , space{space}
    {
    }

public:
    std::optional<win::remnant> remnant;
};

}
