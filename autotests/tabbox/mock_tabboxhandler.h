/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_MOCK_TABBOX_HANDLER_H
#define KWIN_MOCK_TABBOX_HANDLER_H

#include "../../win/tabbox/tabbox_handler.h"

namespace KWin
{
class MockTabBoxHandler : public win::tabbox_handler
{
    Q_OBJECT
public:
    MockTabBoxHandler(QObject* parent = nullptr);
    ~MockTabBoxHandler() override;
    void activate_and_close() override
    {
    }
    std::weak_ptr<win::tabbox_client> active_client() const override;
    void set_active_client(const std::weak_ptr<win::tabbox_client>& client);
    int active_screen() const override
    {
        return 0;
    }
    std::weak_ptr<win::tabbox_client> client_to_add_to_list(win::tabbox_client* client,
                                                            int desktop) const override;
    int current_desktop() const override
    {
        return 1;
    }
    std::weak_ptr<win::tabbox_client> desktop_client() const override
    {
        return std::weak_ptr<win::tabbox_client>();
    }
    QString desktop_name(int desktop) const override
    {
        Q_UNUSED(desktop)
        return "desktop 1";
    }
    QString desktop_name(win::tabbox_client* client) const override
    {
        Q_UNUSED(client)
        return "desktop";
    }
    void elevate_client(win::tabbox_client* c, QWindow* tabbox, bool elevate) const override
    {
        Q_UNUSED(c)
        Q_UNUSED(tabbox)
        Q_UNUSED(elevate)
    }
    virtual void hideOutline()
    {
    }
    std::weak_ptr<win::tabbox_client>
    next_client_focus_chain(win::tabbox_client* client) const override;
    std::weak_ptr<win::tabbox_client> first_client_focus_chain() const override;
    bool is_in_focus_chain(win::tabbox_client* client) const override;
    int next_desktop_focus_chain(int desktop) const override
    {
        Q_UNUSED(desktop)
        return 1;
    }
    int number_of_desktops() const override
    {
        return 1;
    }
    bool is_kwin_compositing() const override
    {
        return false;
    }
    void raise_client(win::tabbox_client* c) const override
    {
        Q_UNUSED(c)
    }
    void restack(win::tabbox_client* c, win::tabbox_client* under) override
    {
        Q_UNUSED(c)
        Q_UNUSED(under)
    }
    virtual void show_outline(const QRect& outline)
    {
        Q_UNUSED(outline)
    }
    win::tabbox_client_list stacking_order() const override
    {
        return win::tabbox_client_list();
    }
    void grabbed_key_event(QKeyEvent* event) const override;

    void highlight_windows(win::tabbox_client* window = nullptr,
                           QWindow* controller = nullptr) override
    {
        Q_UNUSED(window)
        Q_UNUSED(controller)
    }

    bool no_modifier_grab() const override
    {
        return false;
    }

    // mock methods
    std::weak_ptr<win::tabbox_client> createMockWindow(const QString& caption);
    void closeWindow(win::tabbox_client* client);

private:
    std::vector<std::shared_ptr<win::tabbox_client>> m_windows;
    std::weak_ptr<win::tabbox_client> m_activeClient;
};
} // namespace KWin
#endif
