/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <kwin_export.h>

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

namespace TabBox
{
class TabBoxClientImpl;
}

namespace win
{

class KWIN_EXPORT control
{
    bool m_skip_taskbar{false};
    bool m_original_skip_taskbar{false};
    bool m_skip_pager{false};
    bool m_skip_switcher{false};

    std::shared_ptr<TabBox::TabBoxClientImpl> m_tabbox;
    bool m_first_in_tabbox{false};

    QByteArray m_desktop_file_name;
    QIcon m_icon;

    struct {
        bool active{false};
        QString service_name;
        QString object_path;

    } m_application_menu;

    QKeySequence m_shortcut;

    bool m_active{false};
    bool m_keep_above{false};
    bool m_keep_below{false};
    bool m_demands_attention{false};
    bool m_unresponsive{false};

    QTimer* m_auto_raise_timer{nullptr};
    bool m_minimized{false};

    Wrapland::Server::PlasmaWindow* m_wayland_management{nullptr};

    bool m_have_resize_effect{false};

    // While larger 0 the new geometry is remembered but not actually set.
    int m_block_geometry_updates = 0;
    pending_geometry m_pending_geometry_update = pending_geometry::none;

    QRect m_visible_rect_before_geometry_update;
    QRect m_buffer_geometry_before_update_blocking;
    QRect m_frame_geometry_before_update_blocking;

    quicktiles m_quicktiling{quicktiles::none};
    quicktiles m_electric{quicktiles::none};
    bool m_electric_maximizing{false};
    QTimer* m_electric_maximizing_delay{nullptr};

    Toplevel* m_win;

    void minimize(bool avoid_animation);
    void unminimize(bool avoid_animation);

public:
    explicit control(Toplevel* win);
    virtual ~control();

    void setup_tabbox();

    bool skip_pager() const;
    virtual void set_skip_pager(bool set);

    bool skip_switcher() const;
    virtual void set_skip_switcher(bool set);

    bool skip_taskbar() const;
    virtual void set_skip_taskbar(bool set);

    bool original_skip_taskbar() const;
    void set_original_skip_taskbar(bool set);

    std::weak_ptr<TabBox::TabBoxClientImpl> tabbox() const;

    bool first_in_tabbox() const;
    void set_first_in_tabbox(bool is_first);

    QByteArray desktop_file_name() const;
    void set_desktop_file_name(QByteArray const& name);

    QIcon const& icon() const;
    void set_icon(QIcon const& icon);

    bool has_application_menu() const;

    bool application_menu_active() const;
    void set_application_menu_active(bool active);

    QString application_menu_service_name() const;
    QString application_menu_object_path() const;

    void update_application_menu_service_name(QString const& name);
    void update_application_menu_object_path(QString const& path);

    QKeySequence const& shortcut() const;
    void set_shortcut(QString const& shortcut);

    bool active() const;
    void set_active(bool active);

    bool keep_above() const;
    void set_keep_above(bool keep);

    bool keep_below() const;
    void set_keep_below(bool keep);

    void set_demands_attention(bool set);
    bool demands_attention() const;

    bool unresponsive() const;
    void set_unresponsive(bool unresponsive);

    void start_auto_raise();
    void cancel_auto_raise();

    bool minimized() const;
    void set_minimized(bool minimize);

    virtual void update_mouse_grab();

    Wrapland::Server::PlasmaWindow* wayland_management() const;
    void set_wayland_management(Wrapland::Server::PlasmaWindow* plasma_window);
    void destroy_wayland_management();

    bool have_resize_effect() const;
    void update_have_resize_effect();
    void reset_have_resize_effect();

    bool geometry_updates_blocked() const;
    void block_geometry_updates();
    void unblock_geometry_updates();

    pending_geometry pending_geometry_update() const;
    void set_pending_geometry_update(pending_geometry update);

    QRect buffer_geometry_before_update_blocking() const;
    QRect frame_geometry_before_update_blocking() const;
    void update_geometry_before_update_blocking();

    QRect visible_rect_before_geometry_update() const;
    void set_visible_rect_before_geometry_update(QRect const& rect);

    quicktiles electric() const;
    void set_electric(quicktiles tiles);
    bool electric_maximizing() const;
    void set_electric_maximizing(bool maximizing);

    QTimer* electric_maximizing_timer() const;
    void set_electric_maximizing_timer(QTimer* timer);

    quicktiles quicktiling() const;
    void set_quicktiling(quicktiles tiles);
};

}
}
