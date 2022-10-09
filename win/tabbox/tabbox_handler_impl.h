/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_client_impl.h"
#include "tabbox_desktop_chain.h"
#include "tabbox_handler.h"

#include "win/focus_chain_edit.h"
#include "win/scene.h"
#include "win/screen.h"
#include "win/stacking.h"
#include "win/util.h"

namespace KWin::win
{

template<typename Tabbox>
class tabbox_handler_impl : public tabbox_handler
{
public:
    explicit tabbox_handler_impl(Tabbox* tabbox)
        : tabbox_handler([tabbox] { return tabbox->space.scripting->qml_engine; },
                         tabbox->qobject.get())
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

    int active_screen() const override
    {
        auto output = win::get_current_output(m_tabbox->space);
        if (!output) {
            return 0;
        }
        return base::get_output_index(m_tabbox->space.base.outputs, *output);
    }

    std::weak_ptr<tabbox_client> active_client() const override
    {
        if (auto win = m_tabbox->space.stacking.active) {
            return std::visit(overload{[](auto&& win) { return win->control->tabbox(); }}, *win);
        }
        return std::weak_ptr<tabbox_client>();
    }

    int current_desktop() const override
    {
        return m_tabbox->space.virtual_desktop_manager->current();
    }

    QString desktop_name(tabbox_client* client) const override
    {
        auto& vds = m_tabbox->space.virtual_desktop_manager;

        if (auto c = get_client_impl(client)) {
            return std::visit(overload{[&](auto&& win) {
                                  if (on_all_desktops(win)) {
                                      return vds->name(vds->current());
                                  } else {
                                      return vds->name(get_desktop(*win));
                                  }
                              }},
                              c->client());
        }

        return vds->name(vds->current());
    }

    QString desktop_name(int desktop) const override
    {
        return m_tabbox->space.virtual_desktop_manager->name(desktop);
    }

    bool is_kwin_compositing() const override
    {
        return static_cast<bool>(m_tabbox->space.base.render->compositor->scene);
    }

    std::weak_ptr<tabbox_client> next_client_focus_chain(tabbox_client* client) const override
    {
        if (auto c = get_client_impl(client)) {
            auto next
                = focus_chain_next_latest_use(m_tabbox->space.stacking.focus_chain, c->client());
            if (next) {
                return std::visit(overload{[&](auto&& next) { return next->control->tabbox(); }},
                                  *next);
            }
        }
        return std::weak_ptr<tabbox_client>();
    }

    std::weak_ptr<tabbox_client> first_client_focus_chain() const override
    {
        if (auto c = focus_chain_first_latest_use<std::optional<typename Tabbox::window_t>>(
                m_tabbox->space.stacking.focus_chain)) {
            return std::visit(overload{[&](auto&& win) -> std::weak_ptr<tabbox_client> {
                                  return win->control->tabbox();
                              }},
                              *c);
        } else {
            return std::weak_ptr<tabbox_client>();
        }
    }

    bool is_in_focus_chain(tabbox_client* client) const override
    {
        if (auto c = get_client_impl(client)) {
            return contains(m_tabbox->space.stacking.focus_chain.chains.latest_use, c->client());
        }
        return false;
    }

    int next_desktop_focus_chain(int desktop) const override
    {
        return m_desktop_focus_chain->next(desktop);
    }

    int number_of_desktops() const override
    {
        return m_tabbox->space.virtual_desktop_manager->count();
    }

    tabbox_client_list stacking_order() const override
    {
        auto const stacking = m_tabbox->space.stacking.order.stack;
        tabbox_client_list ret;
        for (auto const& win : stacking) {
            std::visit(overload{[&](auto&& win) {
                           if (win->control) {
                               ret.push_back(win->control->tabbox());
                           }
                       }},
                       win);
        }
        return ret;
    }

    void elevate_client(tabbox_client* client, QWindow* tabbox, bool elevate) const override
    {
        std::visit(overload{[elevate](auto&& win) { win::elevate(win, elevate); }},
                   get_client_impl(client)->client());

        if (auto iwin = m_tabbox->space.findInternal(tabbox)) {
            win::elevate(iwin, elevate);
        }
    }

    void raise_client(tabbox_client* client) const override
    {
        std::visit(overload{[this](auto&& win) { win::raise_window(m_tabbox->space, win); }},
                   get_client_impl(client)->client());
    }

    void restack(tabbox_client* c, tabbox_client* under) override
    {
        std::visit(overload{[&, this](auto&& win) {
                       std::visit(overload{[&, this](auto&& under) {
                                      win::restack(m_tabbox->space, win, under, true);
                                  }},
                                  get_client_impl(under)->client());
                   }},
                   get_client_impl(c)->client());
    }

    std::weak_ptr<tabbox_client> client_to_add_to_list(KWin::win::tabbox_client* client,
                                                       int desktop) const override
    {
        if (!client) {
            return {};
        }

        if (!check_desktop(client, desktop) || !check_applications(client)
            || !check_minimized(client) || !check_multi_screen(client)) {
            return {};
        }

        return std::visit(
            overload{[&](auto&& win) -> std::weak_ptr<tabbox_client> {
                if (!win::wants_tab_focus(win) || win->control->skip_switcher()) {
                    return {};
                }

                if (auto modal = find_modal(*win); modal && modal->control && modal != win) {
                    if (!contains_if(
                            client_list(),
                            [modal_client = modal->control->tabbox().lock()](auto const& client) {
                                return client.lock() == modal_client;
                            })) {
                        // Add the modal dialog instead of the main window.
                        return modal->control->tabbox();
                    }
                }

                return win->control->tabbox();
            }},
            get_client_impl(client)->client());
    }

    std::weak_ptr<tabbox_client> desktop_client() const override
    {
        for (auto const& win : m_tabbox->space.stacking.order.stack) {
            auto success{false};
            if (auto ret = std::visit(overload{[&](auto&& win) -> std::weak_ptr<tabbox_client> {
                                          if (win->control && win::is_desktop(win)
                                              && on_current_desktop(win)
                                              && win->topo.central_output
                                                  == win::get_current_output(m_tabbox->space)) {
                                              success = true;
                                              return win->control->tabbox();
                                          }
                                          return {};
                                      }},
                                      win);
                success) {
                return ret;
            }
        }

        return std::weak_ptr<tabbox_client>();
    }

    void activate_and_close() override
    {
        m_tabbox->accept();
    }

    void highlight_windows(tabbox_client* client = nullptr, QWindow* controller = nullptr) override
    {
        auto& effects = m_tabbox->space.base.render->compositor->effects;
        if (!effects) {
            return;
        }

        QVector<EffectWindow*> windows;
        if (client) {
            windows << std::visit(overload{[&](auto&& win) { return win->render->effect.get(); }},
                                  get_client_impl(client)->client());
        }
        if (auto t = m_tabbox->space.findInternal(controller)) {
            windows << t->render->effect.get();
        }

        effects->highlightWindows(windows);
    }

    bool no_modifier_grab() const override
    {
        return m_tabbox->no_modifier_grab();
    }

private:
    using client_impl = tabbox_client_impl<typename Tabbox::window_t>;
    static client_impl* get_client_impl(tabbox_client* client)
    {
        return static_cast<tabbox_client_impl<typename Tabbox::window_t>*>(client);
    }

    bool check_desktop(tabbox_client* client, int desktop) const
    {
        return std::visit(overload{[&](auto&& win) {
                              switch (config().client_desktop_mode()) {
                              case tabbox_config::AllDesktopsClients:
                                  return true;
                              case tabbox_config::ExcludeCurrentDesktopClients:
                                  return !on_desktop(win, desktop);
                              default:
                                  // TabBoxConfig::OnlyCurrentDesktopClients
                                  return on_desktop(win, desktop);
                              }
                          }},
                          get_client_impl(client)->client());
    }

    template<typename Win>
    bool check_one_window_per_application(Win& win) const
    {
        // check if the list already contains an entry of this application
        for (const auto& client_weak : client_list()) {
            auto client = client_weak.lock();
            if (!client) {
                continue;
            }
            if (auto c = dynamic_cast<client_impl*>(client.get())) {
                if (std::visit(overload{[&](auto&& other) {
                                   return win::belong_to_same_client(
                                       other, &win, win::same_client_check::allow_cross_process);
                               }},
                               c->client())) {
                    return false;
                }
            }
        }

        return true;
    }

    template<typename Win>
    bool check_all_windows_current_application(Win& win) const
    {
        auto pointer = tabbox_handle->active_client().lock();
        if (!pointer) {
            return false;
        }
        if (auto c = dynamic_cast<client_impl*>(pointer.get())) {
            if (std::visit(overload{[&](auto&& other) {
                               return win::belong_to_same_client(
                                   other, &win, win::same_client_check::allow_cross_process);
                           }},
                           c->client())) {
                return true;
            }
        }
        return false;
    }

    bool check_applications(tabbox_client* client) const
    {
        return std::visit(overload{[&](auto&& win) {
                              switch (config().client_applications_mode()) {
                              case tabbox_config::OneWindowPerApplication:
                                  return check_one_window_per_application(*win);
                              case tabbox_config::AllWindowsCurrentApplication: {
                                  return check_all_windows_current_application(*win);
                              }
                              default: // tabbox_config::AllWindowsAllApplications
                                  return true;
                              }
                          }},
                          get_client_impl(client)->client());
    }

    bool check_minimized(tabbox_client* client) const
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

    bool check_multi_screen(tabbox_client* client) const
    {
        auto current_window = get_client_impl(client)->client();
        auto current_output = win::get_current_output(m_tabbox->space);

        switch (config().client_multi_screen_mode()) {
        case tabbox_config::IgnoreMultiScreen:
            return true;
        case tabbox_config::ExcludeCurrentScreenClients:
            return std::visit(
                overload{[&](auto&& win) { return win->topo.central_output != current_output; }},
                current_window);
        default: // tabbox_config::OnlyCurrentScreenClients
            return std::visit(
                overload{[&](auto&& win) { return win->topo.central_output == current_output; }},
                current_window);
        }
    }

    Tabbox* m_tabbox;
    tabbox_desktop_chain_manager* m_desktop_focus_chain;
};

}
