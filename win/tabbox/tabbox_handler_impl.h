/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_handler.h"

namespace KWin::win
{

class tabbox_desktop_chain_manager;
class tabbox;

class tabbox_handler_impl : public tabbox_handler
{
public:
    explicit tabbox_handler_impl(win::tabbox* tabbox);
    ~tabbox_handler_impl() override;

    int active_screen() const override;
    std::weak_ptr<tabbox_client> active_client() const override;
    int current_desktop() const override;
    QString desktop_name(tabbox_client* client) const override;
    QString desktop_name(int desktop) const override;
    bool is_kwin_compositing() const override;
    std::weak_ptr<tabbox_client> next_client_focus_chain(tabbox_client* client) const override;
    std::weak_ptr<tabbox_client> first_client_focus_chain() const override;
    bool is_in_focus_chain(tabbox_client* client) const override;
    int next_desktop_focus_chain(int desktop) const override;
    int number_of_desktops() const override;
    tabbox_client_list stacking_order() const override;
    void elevate_client(tabbox_client* c, QWindow* tabbox, bool elevate) const override;
    void raise_client(tabbox_client* client) const override;
    void restack(tabbox_client* c, tabbox_client* under) override;
    std::weak_ptr<tabbox_client> client_to_add_to_list(KWin::win::tabbox_client* client,
                                                       int desktop) const override;
    std::weak_ptr<tabbox_client> desktop_client() const override;
    void activate_and_close() override;
    void highlight_windows(tabbox_client* window = nullptr, QWindow* controller = nullptr) override;
    bool no_modifier_grab() const override;

private:
    bool check_desktop(tabbox_client* client, int desktop) const;
    bool check_applications(tabbox_client* client) const;
    bool check_minimized(tabbox_client* client) const;
    bool check_multi_screen(tabbox_client* client) const;

    win::tabbox* m_tabbox;
    tabbox_desktop_chain_manager* m_desktop_focus_chain;
};

}
