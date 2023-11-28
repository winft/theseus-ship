/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "global_shortcuts_manager.h"
#include "idle.h"
#include "redirect.h"

#include "base/wayland/server.h"
#include "input/dbus/dbus.h"
#include "input/platform.h"
#include "input/types.h"
#include <input/backend/wlroots/backend.h>

#include <QPointF>
#include <Wrapland/Server/display.h>

namespace KWin::input::wayland
{

template<typename Base>
class platform
{
public:
    using base_t = Base;
    using type = platform<Base>;
    using space_t = typename Base::space_t;
    using redirect_t = redirect<space_t>;
    using backend_t = backend::wlroots::backend<type>;

    platform(Base& base, input::config config)
        : base{base}
        , qobject{std::make_unique<platform_qobject>()}
        , config{std::move(config)}
        , backend{*this}
        , xkb{xkb::manager<type>(this)}
        , kde_idle{std::make_unique<Wrapland::Server::kde_idle>(base.server->display.get())}
        , idle_notifier{
              std::make_unique<Wrapland::Server::idle_notifier_v1>(base.server->display.get())}
    {
        qRegisterMetaType<button_state>();
        qRegisterMetaType<key_state>();

        virtual_keyboard = std::make_unique<Wrapland::Server::virtual_keyboard_manager_v1>(
            base.server->display.get());

        QObject::connect(kde_idle.get(),
                         &Wrapland::Server::kde_idle::timeout_created,
                         this->qobject.get(),
                         [this](auto timeout) { idle_setup_kde_device(idle, timeout); });
        QObject::connect(idle_notifier.get(),
                         &Wrapland::Server::idle_notifier_v1::notification_created,
                         this->qobject.get(),
                         [this](auto notif) { idle_setup_notification(idle, notif); });
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    virtual ~platform() = default;

    std::unique_ptr<redirect<space_t>> integrate_space(space_t& space) const
    {
        return std::make_unique<redirect<space_t>>(space);
    }

    void setup_action_for_global_accel(QAction* /*action*/)
    {
    }

    void registerShortcut(QKeySequence const& /*shortcut*/, QAction* /*action*/)
    {
    }

    /**
     * @overload
     *
     * Like registerShortcut, but also connects QAction::triggered to the @p slot on @p receiver.
     * It's recommended to use this method as it ensures that the X11 timestamp is updated prior
     * to the @p slot being invoked. If not using this overload it's required to ensure that
     * registerShortcut is called before connecting to QAction's triggered signal.
     */
    template<typename T, typename Slot>
    void registerShortcut(const QKeySequence& shortcut, QAction* action, T* receiver, Slot slot)
    {
        registerShortcut(shortcut, action);
        QObject::connect(action, &QAction::triggered, receiver, slot);
    }

    void install_shortcuts()
    {
        this->shortcuts = std::make_unique<global_shortcuts_manager>();
        this->shortcuts->init();
        setup_touchpad_shortcuts();
    }

    void update_keyboard_leds(input::keyboard_leds leds)
    {
        for (auto& keyboard : this->keyboards) {
            if (keyboard->control) {
                keyboard->control->update_leds(leds);
            }
        }
    }

    void toggle_touchpads()
    {
        auto changed{false};
        touchpads_enabled = !touchpads_enabled;

        for (auto& pointer : this->pointers) {
            if (!pointer->control) {
                continue;
            }
            auto& ctrl = pointer->control;
            if (!ctrl->is_touchpad()) {
                continue;
            }

            auto old_enabled = ctrl->is_enabled();
            ctrl->set_enabled(touchpads_enabled);

            if (old_enabled != ctrl->is_enabled()) {
                changed = true;
            }
        }

        if (changed) {
            dbus::inform_touchpad_toggle(touchpads_enabled);
        }
    }

    void enable_touchpads()
    {
        if (touchpads_enabled) {
            return;
        }
        toggle_touchpads();
    }

    void disable_touchpads()
    {
        if (!touchpads_enabled) {
            return;
        }
        toggle_touchpads();
    }

    bool are_mod_keys_depressed(QKeySequence const& seq) const
    {
        const int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;
        auto const mods = xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(*this);

        if ((mod & Qt::SHIFT) && mods.testFlag(Qt::ShiftModifier)) {
            return true;
        }
        if ((mod & Qt::CTRL) && mods.testFlag(Qt::ControlModifier)) {
            return true;
        }
        if ((mod & Qt::ALT) && mods.testFlag(Qt::AltModifier)) {
            return true;
        }
        if ((mod & Qt::META) && mods.testFlag(Qt::MetaModifier)) {
            return true;
        }

        return false;
    }

    Base& base;
    std::unique_ptr<platform_qobject> qobject;
    input::config config;

    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;
    std::vector<switch_device*> switches;
    std::vector<touch*> touchs;

    backend_t backend;
    std::unique_ptr<Wrapland::Server::virtual_keyboard_manager_v1> virtual_keyboard;

    input::xkb::manager<type> xkb;
    std::unique_ptr<global_shortcuts_manager> shortcuts;
    std::unique_ptr<dbus::device_manager<type>> dbus;
    input::idle idle;
    std::unique_ptr<Wrapland::Server::kde_idle> kde_idle;
    std::unique_ptr<Wrapland::Server::idle_notifier_v1> idle_notifier;

private:
    void setup_touchpad_shortcuts()
    {
        auto toggle_action = new QAction(this->qobject.get());
        auto on_action = new QAction(this->qobject.get());
        auto off_action = new QAction(this->qobject.get());

        constexpr auto const component{"kcm_touchpad"};

        toggle_action->setObjectName(QStringLiteral("Toggle Touchpad"));
        toggle_action->setProperty("componentName", component);
        on_action->setObjectName(QStringLiteral("Enable Touchpad"));
        on_action->setProperty("componentName", component);
        off_action->setObjectName(QStringLiteral("Disable Touchpad"));
        off_action->setProperty("componentName", component);

        shortcuts->register_keyboard_default_shortcut(toggle_action,
                                                      {QKeySequence{Qt::Key_TouchpadToggle}});
        shortcuts->register_keyboard_shortcut(toggle_action,
                                              {QKeySequence{Qt::Key_TouchpadToggle}});
        shortcuts->register_keyboard_default_shortcut(on_action,
                                                      {QKeySequence{Qt::Key_TouchpadOn}});
        shortcuts->register_keyboard_shortcut(on_action, {QKeySequence{Qt::Key_TouchpadOn}});
        shortcuts->register_keyboard_default_shortcut(off_action,
                                                      {QKeySequence{Qt::Key_TouchpadOff}});
        shortcuts->register_keyboard_shortcut(off_action, {QKeySequence{Qt::Key_TouchpadOff}});

        registerShortcut(Qt::Key_TouchpadToggle, toggle_action);
        registerShortcut(Qt::Key_TouchpadOn, on_action);
        registerShortcut(Qt::Key_TouchpadOff, off_action);

        QObject::connect(toggle_action, &QAction::triggered, this->qobject.get(), [this] {
            toggle_touchpads();
        });
        QObject::connect(
            on_action, &QAction::triggered, this->qobject.get(), [this] { enable_touchpads(); });
        QObject::connect(
            off_action, &QAction::triggered, this->qobject.get(), [this] { disable_touchpads(); });
    }

    bool touchpads_enabled{true};
};

template<typename Input>
void add_dbus(Input* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager<Input>>(*platform);
}

}
