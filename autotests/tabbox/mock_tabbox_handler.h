/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MOCK_TABBOX_HANDLER_H
#define KWIN_MOCK_TABBOX_HANDLER_H

#include "win/tabbox/tabbox_handler.h"

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
    win::tabbox_client* active_client() const override;
    void set_active_client(win::tabbox_client* client);
    int active_screen() const override
    {
        return 0;
    }
    win::tabbox_client* client_to_add_to_list(win::tabbox_client* client,
                                              int desktop) const override;
    int current_desktop() const override
    {
        return 1;
    }
    win::tabbox_client* desktop_client() const override
    {
        return {};
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
    win::tabbox_client* next_client_focus_chain(win::tabbox_client* client) const override;
    win::tabbox_client* first_client_focus_chain() const override;
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
    win::tabbox_client* createMockWindow(const QString& caption);
    void closeWindow(win::tabbox_client* client);

private:
    std::vector<std::unique_ptr<win::tabbox_client>> m_windows;
    win::tabbox_client* m_activeClient;
};
} // namespace KWin
#endif
