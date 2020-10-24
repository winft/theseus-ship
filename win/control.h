/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QIcon>

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

    QIcon m_icon;

    bool m_active{false};
    bool m_keep_above{false};
    bool m_keep_below{false};
    bool m_demands_attention{false};
    QTimer* m_auto_raise_timer{nullptr};
    bool m_minimized{false};

    Wrapland::Server::PlasmaWindow* m_wayland_management{nullptr};

    Toplevel* m_win;

    void minimize(bool avoid_animation);
    void unminimize(bool avoid_animation);

public:
    explicit control(Toplevel* win);
    virtual ~control() = default;

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

    QIcon const& icon() const;
    void set_icon(QIcon const& icon);

    bool active() const;
    void set_active(bool active);

    bool keep_above() const;
    void set_keep_above(bool keep);

    bool keep_below() const;
    void set_keep_below(bool keep);

    void set_demands_attention(bool set);
    bool demands_attention() const;

    void start_auto_raise();
    void cancel_auto_raise();

    bool minimized() const;
    void set_minimized(bool minimize);

    virtual void update_mouse_grab();

    Wrapland::Server::PlasmaWindow* wayland_management() const;
    void set_wayland_management(Wrapland::Server::PlasmaWindow* plasma_window);
    void destroy_wayland_management();
};

}
}
