/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <memory>
#include <vector>

namespace Wrapland::Server
{
class input_method_keyboard_grab_v2;
class input_method_manager_v2;
class input_method_popup_surface_v2;
class text_input_manager_v3;
}

namespace KWin
{

namespace base::wayland
{
class server;
}

namespace win::wayland
{
class window;
}

namespace input
{

template<typename>
class keyboard_grab;

namespace wayland
{

using im_keyboard_grab_v2 = keyboard_grab<Wrapland::Server::input_method_keyboard_grab_v2>;

class KWIN_EXPORT input_method : public QObject
{
    Q_OBJECT
public:
    input_method(base::wayland::server* server);
    ~input_method();

private:
    void input_method_v2_changed();
    void handle_keyboard_grabbed(Wrapland::Server::input_method_keyboard_grab_v2* grab);
    void handle_popup_surface_created(Wrapland::Server::input_method_popup_surface_v2* popup);

    void activate_filters();
    void activate_popups();

    void deactivate();

    struct {
        QMetaObject::Connection popup_created;
        QMetaObject::Connection keyboard_grabbed;
    } notifiers;

    std::vector<win::wayland::window*> popups;
    std::vector<std::unique_ptr<im_keyboard_grab_v2>> filters;

    std::unique_ptr<Wrapland::Server::text_input_manager_v3> text_input_manager_v3;
    std::unique_ptr<Wrapland::Server::input_method_manager_v2> input_method_manager_v2;
};

}
}
}
