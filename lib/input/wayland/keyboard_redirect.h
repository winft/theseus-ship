/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/keyboard_redirect.h"
#include "input/spies/keyboard_repeat.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "input/xkb/layout_manager.h"
#include "input/xkb/manager.h"

#include <KScreenLocker/KsldApp>
#include <memory>

namespace KWin::input::wayland
{

template<typename Redirect>
class key_state_changed_spy : public event_spy<Redirect>
{
public:
    key_state_changed_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
    {
    }

    void key(key_event const& event) override
    {
        Q_EMIT this->redirect.qobject->keyStateChanged(event.keycode, event.state);
    }
};

template<typename Redirect>
class modifiers_changed_spy : public event_spy<Redirect>
{
public:
    modifiers_changed_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
        , m_modifiers()
    {
    }

    void key(key_event const& event) override
    {
        if (auto& xkb = event.base.dev->xkb) {
            updateModifiers(xkb->qt_modifiers);
        }
    }

    void updateModifiers(Qt::KeyboardModifiers mods)
    {
        if (mods == m_modifiers) {
            return;
        }
        Q_EMIT this->redirect.qobject->keyboardModifiersChanged(mods, m_modifiers);
        m_modifiers = mods;
    }

private:
    Qt::KeyboardModifiers m_modifiers;
};

template<typename Redirect>
class keyboard_redirect
{
public:
    using platform_t = typename Redirect::platform_t;
    using space_t = typename platform_t::base_t::space_t;
    using window_t = typename space_t::window_t;
    using layout_manager_t = xkb::layout_manager<Redirect>;

    explicit keyboard_redirect(Redirect* redirect)
        : qobject{std::make_unique<keyboard_redirect_qobject>()}
        , redirect{redirect}
    {
    }

    void init()
    {
        redirect->platform.xkb.numlock_config = redirect->platform.config.main;

        redirect->m_spies.push_back(new key_state_changed_spy(*redirect));
        modifiers_spy = new modifiers_changed_spy(*redirect);
        redirect->m_spies.push_back(modifiers_spy);

        layout_manager
            = std::make_unique<layout_manager_t>(*redirect, redirect->platform.config.xkb);

        if (redirect->platform.base.server->has_global_shortcut_support()) {
            redirect->m_spies.push_back(new modifier_only_shortcuts_spy(*redirect));
        }

        auto keyRepeatSpy = new keyboard_repeat_spy(*redirect);
        QObject::connect(keyRepeatSpy->qobject.get(),
                         &keyboard_repeat_spy_qobject::key_repeated,
                         qobject.get(),
                         [this](auto const& event) { process_key_repeat(event); });
        redirect->m_spies.push_back(keyRepeatSpy);

        QObject::connect(redirect->space.qobject.get(),
                         &space_t::qobject_t::clientActivated,
                         qobject.get(),
                         [this] {
                             QObject::disconnect(m_activeClientSurfaceChangedConnection);
                             if (auto act = redirect->space.stacking.active) {
                                 std::visit(overload{[this](auto&& win) {
                                                m_activeClientSurfaceChangedConnection
                                                    = QObject::connect(
                                                        win->qobject.get(),
                                                        &win::window_qobject::surfaceChanged,
                                                        qobject.get(),
                                                        [this] { update(); });
                                            }},
                                            *act);
                             } else {
                                 m_activeClientSurfaceChangedConnection = QMetaObject::Connection();
                             }
                             update();
                         });
        if (redirect->platform.base.server->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             qobject.get(),
                             [this] { update(); });
        }
    }

    void update()
    {
        auto seat = redirect->platform.base.server->seat();
        if (!seat->hasKeyboard()) {
            return;
        }

        // TODO: this needs better integration
        std::optional<window_t> found;
        auto const& stacking = redirect->space.stacking.order.stack;
        if (!stacking.empty()) {
            auto it = stacking.end();
            do {
                --it;
                if (std::visit(overload{[&](typename space_t::wayland_window* win) {
                                            if (win->remnant) {
                                                // a deleted window doesn't get mouse events
                                                return false;
                                            }
                                            if (!win->render_data.ready_for_painting) {
                                                return false;
                                            }
                                            if (!win->layer_surface
                                                || !win->has_exclusive_keyboard_interactivity()) {
                                                return false;
                                            }
                                            found = win;
                                            return true;
                                        },
                                        [&](auto&&) { return false; }},
                               *it)) {
                    break;
                }
            } while (it != stacking.begin());
        }

        if (!found && !redirect->isSelectingWindow()) {
            found = redirect->space.stacking.active;
        }
        if (!found) {
            seat->setFocusedKeyboardSurface(nullptr);
            return;
        }

        std::visit(overload{[&](auto&& found) {
                       if constexpr (requires(decltype(found) win) { win->surface; }) {
                           if (!found->surface) {
                               seat->setFocusedKeyboardSurface(nullptr);
                               return;
                           }
                           if (found->surface != seat->keyboards().get_focus().surface) {
                               seat->setFocusedKeyboardSurface(found->surface);
                           }
                       } else {
                           seat->setFocusedKeyboardSurface(nullptr);
                       }
                   }},
                   *found);
    }

    void process_key(key_event const& event)
    {
        auto& xkb = event.base.dev->xkb;

        keyboard_redirect_prepare_key<Redirect>(*this, event);

        process_filters(redirect->m_filters,
                        std::bind(&event_filter<Redirect>::key, std::placeholders::_1, event));
        xkb->forward_modifiers();
    }

    void process_key_repeat(key_event const& event)
    {
        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::key_repeat, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::key_repeat, std::placeholders::_1, event));
    }

    void process_modifiers(modifiers_event const& event)
    {
        auto const& xkb = event.base.dev->xkb.get();

        // TODO: send to proper Client and also send when active Client changes
        xkb->update_modifiers(event.depressed, event.latched, event.locked, event.group);

        modifiers_spy->updateModifiers(xkb->qt_modifiers);
    }

    std::unique_ptr<keyboard_redirect_qobject> qobject;
    Redirect* redirect;

private:
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy<Redirect>* modifiers_spy{nullptr};

    std::unique_ptr<layout_manager_t> layout_manager;
};

}
