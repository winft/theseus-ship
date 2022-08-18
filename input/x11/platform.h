/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"
#include "redirect.h"
#include "window_selector.h"

#include "config-kwin.h"
#include "input/platform.h"
#include "main.h"

#if HAVE_X11_XINPUT
#include "input/x11/xinput_integration.h"
#endif

#include <KGlobalAccel>
#include <QX11Info>
#include <memory>

namespace KWin::input::x11
{

template<typename Base>
class platform : public input::platform<Base>
{
public:
    using type = platform<Base>;
    using redirect_t = x11::redirect<type>;
    using abstract_type = input::platform<Base>;
    using space_t = typename abstract_type::base_t::space_t;

    platform(Base& base)
        : input::platform<Base>(base)
        , xkb{xkb::manager<type>(this)}
    {
#if HAVE_X11_XINPUT
        if (!qEnvironmentVariableIsSet("KWIN_NO_XI2")) {
            xinput.reset(new xinput_integration<type>(QX11Info::display(), this));
            xinput->init();
            if (!xinput->hasXinput()) {
                xinput.reset();
            } else {
                QObject::connect(kwinApp(),
                                 &Application::startup_finished,
                                 xinput.get(),
                                 &xinput_integration<type>::startListening);
            }
        }
#endif
        create_cursor();
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;

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
                         kwinApp(),
                         [action](QAction* triggeredAction, bool /*active*/) {
                             if (triggeredAction != action) {
                                 return;
                             }

                             QVariant timestamp
                                 = action->property("org.kde.kglobalaccel.activationTimestamp");
                             bool ok = false;
                             const quint32 t = timestamp.toULongLong(&ok);
                             if (ok) {
                                 kwinApp()->setX11Time(t);
                             }
                         });
    }

    void registerShortcut(QKeySequence const& shortcut, QAction* action)
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

    void
    start_interactive_window_selection(std::function<void(typename space_t::window_t*)> callback,
                                       QByteArray const& cursorName = QByteArray())
    {
        if (!window_sel) {
            window_sel.reset(new window_selector(*this));
        }
        window_sel->start(callback, cursorName);
    }

    void start_interactive_position_selection(std::function<void(QPoint const&)> callback)
    {
        if (!window_sel) {
            window_sel.reset(new window_selector(*this));
        }
        window_sel->start(callback);
    }

    redirect_t* redirect{nullptr};

#if HAVE_X11_XINPUT
    std::unique_ptr<xinput_integration<type>> xinput;
#endif
    std::unique_ptr<x11::cursor> cursor;
    std::unique_ptr<window_selector<type>> window_sel;

    input::xkb::manager<type> xkb;
    std::unique_ptr<dbus::device_manager<type>> dbus;

private:
#if HAVE_X11_XINPUT
    void create_cursor()
    {
        auto const is_xinput_avail = xinput != nullptr;
        this->cursor = std::make_unique<x11::cursor>(is_xinput_avail);

        if (is_xinput_avail) {
            xinput->setCursor(static_cast<x11::cursor*>(this->cursor.get()));

            xkb.setConfig(kwinApp()->kxkbConfig());
            xkb.reconfigure();
        }
    }
#else
    void create_cursor()
    {
        cursor = std::make_unique<x11::cursor>(false);
    }
#endif
};

}
