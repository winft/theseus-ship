/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "global_shortcuts_manager.h"
#include "redirect.h"

#include "config-kwin.h"
#include "input/platform.h"

#include <KGlobalAccel>
#include <memory>

namespace KWin::input::x11
{

template<typename Base>
class platform
{
public:
    using base_t = Base;
    using type = platform<Base>;
    using space_t = typename Base::space_t;
    using redirect_t = redirect<space_t>;

    platform(Base& base)
        : qobject{std::make_unique<platform_qobject>()}
        , config{input::config(KConfig::NoGlobals)}
        , xkb{xkb::manager<type>(this)}
        , shortcuts{std::make_unique<global_shortcuts_manager>()}
        , base{base}
    {
        qRegisterMetaType<button_state>();
        qRegisterMetaType<key_state>();
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    virtual ~platform() = default;

    std::unique_ptr<redirect<space_t>> integrate_space(space_t& space) const
    {
        return std::make_unique<redirect<space_t>>(space);
    }

    /**
     * Platform specific preparation for an @p action which is used for KGlobalAccel.
     *
     * A platform might need to do preparation for an @p action before
     * it can be used with KGlobalAccel.
     *
     * Code using KGlobalAccel should invoke this method for the @p action
     * prior to setting up any shortcuts and connections.
     *
     * The default implementation does nothing.
     *
     * @param action The action which will be used with KGlobalAccel.
     * @since 5.10
     */
    void setup_action_for_global_accel(QAction* action)
    {
        QObject::connect(KGlobalAccel::self(),
                         &KGlobalAccel::globalShortcutActiveChanged,
                         qobject.get(),
                         [this, action](QAction* triggeredAction, bool /*active*/) {
                             if (triggeredAction != action) {
                                 return;
                             }

                             QVariant timestamp
                                 = action->property("org.kde.kglobalaccel.activationTimestamp");
                             bool ok = false;
                             const quint32 t = timestamp.toULongLong(&ok);
                             if (ok) {
                                 base::x11::advance_time(base.x11_data, t);
                             }
                         });
    }

    void registerShortcut(QKeySequence const& /*shortcut*/, QAction* action)
    {
        setup_action_for_global_accel(action);
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

    std::unique_ptr<platform_qobject> qobject;
    input::config config;

    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;

    input::xkb::manager<type> xkb;
    std::unique_ptr<global_shortcuts_manager> shortcuts;
    std::unique_ptr<dbus::device_manager<type>> dbus;

    Base& base;
};

}
