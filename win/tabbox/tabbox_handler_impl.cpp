/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "tabbox_handler_impl.h"

#include "tabbox_client_model.h"
#include "tabbox_config.h"
#include "tabbox_desktop_chain.h"
#include "tabbox_desktop_model.h"
#include "tabbox_logging.h"
#include "tabbox_x11_filter.h"

#include "base/os/kkeyserver.h"
#include "base/platform.h"
#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "render/effect/window_impl.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/focus_chain.h"
#include "win/meta.h"
#include "win/scene.h"
#include "win/screen.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/util.h"
#include "win/virtual_desktops.h"
#include "win/x11/window.h"

#include <QAction>
#include <QKeyEvent>
// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <KLocalizedString>
// X11
#include <X11/keysym.h>
#include <X11/keysymdef.h>
// xcb
#include <xcb/xcb_keysyms.h>

namespace KWin::win
{

tabbox_handler_impl::tabbox_handler_impl(win::tabbox* tabbox)
    : tabbox_handler([tabbox] { return tabbox->space.scripting->qmlEngine(); }, tabbox)
    , m_tabbox(tabbox)
    , m_desktop_focus_chain(new tabbox_desktop_chain_manager(this))
{
    // connects for DesktopFocusChainManager
    auto& vds = tabbox->space.virtual_desktop_manager;
    connect(vds->qobject.get(),
            &win::virtual_desktop_manager_qobject::countChanged,
            m_desktop_focus_chain,
            &tabbox_desktop_chain_manager::resize);
    connect(vds->qobject.get(),
            &win::virtual_desktop_manager_qobject::currentChanged,
            m_desktop_focus_chain,
            &tabbox_desktop_chain_manager::add_desktop);
}

tabbox_handler_impl::~tabbox_handler_impl()
{
}

int tabbox_handler_impl::active_screen() const
{
    auto output = win::get_current_output(m_tabbox->space);
    if (!output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *output);
}

int tabbox_handler_impl::current_desktop() const
{
    return m_tabbox->space.virtual_desktop_manager->current();
}

QString tabbox_handler_impl::desktop_name(tabbox_client* client) const
{
    auto& vds = m_tabbox->space.virtual_desktop_manager;

    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        if (!c->client()->isOnAllDesktops())
            return vds->name(c->client()->desktop());
    }

    return vds->name(vds->current());
}

QString tabbox_handler_impl::desktop_name(int desktop) const
{
    return m_tabbox->space.virtual_desktop_manager->name(desktop);
}

std::weak_ptr<tabbox_client>
tabbox_handler_impl::next_client_focus_chain(tabbox_client* client) const
{
    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        auto next = focus_chain_next_latest_use(m_tabbox->space.focus_chain, c->client());
        if (next) {
            return next->control->tabbox();
        }
    }
    return std::weak_ptr<tabbox_client>();
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::first_client_focus_chain() const
{
    if (auto c = focus_chain_first_latest_use<Toplevel>(m_tabbox->space.focus_chain)) {
        return c->control->tabbox();
    } else {
        return std::weak_ptr<tabbox_client>();
    }
}

bool tabbox_handler_impl::is_in_focus_chain(tabbox_client* client) const
{
    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        return contains(m_tabbox->space.focus_chain.chains.latest_use, c->client());
    }
    return false;
}

int tabbox_handler_impl::next_desktop_focus_chain(int desktop) const
{
    return m_desktop_focus_chain->next(desktop);
}

int tabbox_handler_impl::number_of_desktops() const
{
    return m_tabbox->space.virtual_desktop_manager->count();
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::active_client() const
{
    if (auto win = m_tabbox->space.active_client) {
        return win->control->tabbox();
    } else {
        return std::weak_ptr<tabbox_client>();
    }
}

bool tabbox_handler_impl::check_desktop(tabbox_client* client, int desktop) const
{
    auto current = (static_cast<tabbox_client_impl*>(client))->client();

    switch (config().client_desktop_mode()) {
    case tabbox_config::AllDesktopsClients:
        return true;
    case tabbox_config::ExcludeCurrentDesktopClients:
        return !current->isOnDesktop(desktop);
    default: // TabBoxConfig::OnlyCurrentDesktopClients
        return current->isOnDesktop(desktop);
    }
}

bool tabbox_handler_impl::check_applications(tabbox_client* client) const
{
    auto current = (static_cast<tabbox_client_impl*>(client))->client();
    tabbox_client_impl* c;

    switch (config().client_applications_mode()) {
    case tabbox_config::OneWindowPerApplication:
        // check if the list already contains an entry of this application
        for (const auto& client_weak : client_list()) {
            auto client = client_weak.lock();
            if (!client) {
                continue;
            }
            if ((c = dynamic_cast<tabbox_client_impl*>(client.get()))) {
                if (win::belong_to_same_client(
                        c->client(), current, win::same_client_check::allow_cross_process)) {
                    return false;
                }
            }
        }
        return true;
    case tabbox_config::AllWindowsCurrentApplication: {
        auto pointer = tabbox_handle->active_client().lock();
        if (!pointer) {
            return false;
        }
        if ((c = dynamic_cast<tabbox_client_impl*>(pointer.get()))) {
            if (win::belong_to_same_client(
                    c->client(), current, win::same_client_check::allow_cross_process)) {
                return true;
            }
        }
        return false;
    }
    default: // tabbox_config::AllWindowsAllApplications
        return true;
    }
}

bool tabbox_handler_impl::check_minimized(tabbox_client* client) const
{
    switch (config().client_minimized_mode()) {
    case tabbox_config::ExcludeMinimizedClients:
        return !client->is_minimized();
    case tabbox_config::OnlyMinimizedClients:
        return client->is_minimized();
    default: // tabbox_config::IgnoreMinimizedStatus
        return true;
    }
}

bool tabbox_handler_impl::check_multi_screen(tabbox_client* client) const
{
    auto current_window = (static_cast<tabbox_client_impl*>(client))->client();
    auto current_output = win::get_current_output(m_tabbox->space);

    switch (config().client_multi_screen_mode()) {
    case tabbox_config::IgnoreMultiScreen:
        return true;
    case tabbox_config::ExcludeCurrentScreenClients:
        return current_window->central_output != current_output;
    default: // tabbox_config::OnlyCurrentScreenClients
        return current_window->central_output == current_output;
    }
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::client_to_add_to_list(tabbox_client* client,
                                                                        int desktop) const
{
    if (!client) {
        return std::weak_ptr<tabbox_client>();
    }
    Toplevel* ret = nullptr;
    auto current = (static_cast<tabbox_client_impl*>(client))->client();

    bool add_client = check_desktop(client, desktop) && check_applications(client)
        && check_minimized(client) && check_multi_screen(client);
    add_client = add_client && win::wants_tab_focus(current) && !current->control->skip_switcher();
    if (add_client) {
        // don't add windows that have modal dialogs
        auto modal = current->findModal();
        if (!modal || !modal->control || modal == current) {
            ret = current;
        } else {
            auto const cl = client_list();
            if (std::find_if(cl.cbegin(),
                             cl.cend(),
                             [modal_client = modal->control->tabbox().lock()](auto const& client) {
                                 return client.lock() == modal_client;
                             })
                == cl.cend()) {
                ret = modal;
            }
        }
    }
    return ret ? ret->control->tabbox() : std::weak_ptr<tabbox_client>();
}

tabbox_client_list tabbox_handler_impl::stacking_order() const
{
    auto const stacking = m_tabbox->space.stacking_order->stack;
    tabbox_client_list ret;
    for (auto const& toplevel : stacking) {
        if (toplevel->control) {
            ret.push_back(toplevel->control->tabbox());
        }
    }
    return ret;
}

bool tabbox_handler_impl::is_kwin_compositing() const
{
    return static_cast<bool>(m_tabbox->space.render.scene);
}

void tabbox_handler_impl::raise_client(tabbox_client* c) const
{
    win::raise_window(&m_tabbox->space, static_cast<tabbox_client_impl*>(c)->client());
}

void tabbox_handler_impl::restack(tabbox_client* c, tabbox_client* under)
{
    win::restack(&m_tabbox->space,
                 static_cast<tabbox_client_impl*>(c)->client(),
                 static_cast<tabbox_client_impl*>(under)->client(),
                 true);
}

void tabbox_handler_impl::elevate_client(tabbox_client* c, QWindow* tabbox, bool b) const
{
    auto cl = static_cast<tabbox_client_impl*>(c)->client();
    win::elevate(cl, b);
    if (auto w = m_tabbox->space.findInternal(tabbox)) {
        win::elevate(w, b);
    }
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::desktop_client() const
{
    for (auto const& window : m_tabbox->space.stacking_order->stack) {
        if (window->control && win::is_desktop(window) && window->isOnCurrentDesktop()
            && window->central_output == win::get_current_output(m_tabbox->space)) {
            return window->control->tabbox();
        }
    }
    return std::weak_ptr<tabbox_client>();
}

void tabbox_handler_impl::activate_and_close()
{
    m_tabbox->accept();
}

void tabbox_handler_impl::highlight_windows(tabbox_client* window, QWindow* controller)
{
    auto& effects = m_tabbox->space.render.effects;
    if (!effects) {
        return;
    }

    QVector<EffectWindow*> windows;
    if (window) {
        windows << static_cast<tabbox_client_impl*>(window)->client()->render->effect.get();
    }
    if (auto t = m_tabbox->space.findInternal(controller)) {
        windows << t->render->effect.get();
    }

    effects->highlightWindows(windows);
}

bool tabbox_handler_impl::no_modifier_grab() const
{
    return m_tabbox->no_modifier_grab();
}

}
