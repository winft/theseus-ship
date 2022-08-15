/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "appmenu.h"
#include "structs.h"
#include "types.h"
#include "virtual_desktops.h"

#include "kwin_export.h"
#include "rules/window.h"
#include "scripting/window.h"

#include <QIcon>
#include <QKeySequence>
#include <QRect>

#include <memory>

namespace Wrapland
{
namespace Server
{
class PlasmaWindow;
}
}

class QTimer;

namespace KWin
{
class Toplevel;

namespace win
{

namespace deco
{
class palette;
}

template<typename Window>
class tabbox_client_impl;

class KWIN_EXPORT control
{
public:
    explicit control(Toplevel* win);
    virtual ~control();

    void setup_tabbox();

    virtual void set_desktops(QVector<virtual_desktop*> desktops) = 0;

    bool skip_pager() const;
    virtual void set_skip_pager(bool set);

    bool skip_switcher() const;
    virtual void set_skip_switcher(bool set);

    bool skip_taskbar() const;
    virtual void set_skip_taskbar(bool set);

    std::weak_ptr<win::tabbox_client_impl<Toplevel>> tabbox() const;

    bool has_application_menu() const;
    void set_application_menu_active(bool active);

    void update_application_menu(appmenu_address const& address);

    void set_shortcut(QString const& shortcut);

    void set_unresponsive(bool unresponsive);

    void start_auto_raise();
    void cancel_auto_raise();

    virtual void update_mouse_grab();

    virtual void destroy_plasma_wayland_integration();

    void update_have_resize_effect();

    virtual QSize adjusted_frame_size(QSize const& frame_size, size_mode mode);
    virtual bool can_fullscreen() const;
    virtual void destroy_decoration();

    void setup_color_scheme();

    void remove_rule(rules::ruling* r);
    void discard_temporary_rules();

    std::unique_ptr<scripting::window_impl> scripting;
    Wrapland::Server::PlasmaWindow* plasma_wayland_integration{nullptr};

    bool active{false};
    bool keep_above{false};
    bool keep_below{false};
    bool demands_attention{false};
    bool unresponsive{false};
    bool original_skip_taskbar{false};

    win::appmenu appmenu;
    QKeySequence shortcut;
    QIcon icon;

    quicktiles quicktiling{quicktiles::none};
    quicktiles electric{quicktiles::none};
    bool electric_maximizing{false};
    QTimer* electric_maximizing_delay{nullptr};

    bool have_resize_effect{false};

    bool first_in_tabbox{false};
    QByteArray desktop_file_name;

    bool fullscreen{false};
    bool minimized{false};
    win::move_resize_op move_resize;
    win::deco_impl<Toplevel> deco;
    win::palette palette;
    rules::window rules;

private:
    void minimize(bool avoid_animation);
    void unminimize(bool avoid_animation);

    bool m_skip_taskbar{false};
    bool m_skip_pager{false};
    bool m_skip_switcher{false};

    std::shared_ptr<win::tabbox_client_impl<Toplevel>> m_tabbox;

    QTimer* m_auto_raise_timer{nullptr};

    Toplevel* m_win;
};

}
}
