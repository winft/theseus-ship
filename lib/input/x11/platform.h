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
#include <X11/keysym.h>
#include <memory>
#include <xcb/xcb_keysyms.h>

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

    bool are_mod_keys_depressed(QKeySequence const& seq) const
    {
        uint rgKeySyms[10];
        int nKeySyms = 0;
        int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;

        if (mod & Qt::SHIFT) {
            rgKeySyms[nKeySyms++] = XK_Shift_L;
            rgKeySyms[nKeySyms++] = XK_Shift_R;
        }
        if (mod & Qt::CTRL) {
            rgKeySyms[nKeySyms++] = XK_Control_L;
            rgKeySyms[nKeySyms++] = XK_Control_R;
        }
        if (mod & Qt::ALT) {
            rgKeySyms[nKeySyms++] = XK_Alt_L;
            rgKeySyms[nKeySyms++] = XK_Alt_R;
        }
        if (mod & Qt::META) {
            // It would take some code to determine whether the Win key
            // is associated with Super or Meta, so check for both.
            // See bug #140023 for details.
            rgKeySyms[nKeySyms++] = XK_Super_L;
            rgKeySyms[nKeySyms++] = XK_Super_R;
            rgKeySyms[nKeySyms++] = XK_Meta_L;
            rgKeySyms[nKeySyms++] = XK_Meta_R;
        }

        return are_key_syms_depressed(rgKeySyms, nKeySyms);
    }

    bool grab_keyboard(xcb_window_t w)
    {
        if (QWidget::keyboardGrabber() != nullptr) {
            return false;
        }
        if (keyboard_grabbed) {
            qCDebug(KWIN_INPUT) << "Failed to grab X Keyboard: already grabbed by us";
            return false;
        }
        if (qApp->activePopupWidget() != nullptr) {
            qCDebug(KWIN_INPUT) << "Failed to grab X Keyboard: popup widget active";
            return false;
        }

        auto const& data = base.x11_data;

        if (w == XCB_WINDOW_NONE) {
            w = data.root_window;
        }

        auto const cookie = xcb_grab_keyboard_unchecked(
            data.connection, false, w, data.time, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        unique_cptr<xcb_grab_keyboard_reply_t> grab(
            xcb_grab_keyboard_reply(data.connection, cookie, nullptr));

        if (!grab) {
            qCDebug(KWIN_INPUT) << "Failed to grab X Keyboard: grab null";
            return false;
        }

        if (grab->status != XCB_GRAB_STATUS_SUCCESS) {
            qCDebug(KWIN_INPUT) << "Failed to grab X Keyboard: grab failed with status"
                                << grab->status;
            return false;
        }

        keyboard_grabbed = true;
        return true;
    }

    bool grab_keyboard()
    {
        return grab_keyboard(XCB_WINDOW_NONE);
    }

    void ungrab_keyboard()
    {
        if (!keyboard_grabbed) {
            // grabXKeyboard() may fail sometimes, so don't fail, but at least warn anyway
            qCDebug(KWIN_INPUT) << "ungrabXKeyboard() called but keyboard not grabbed!";
        }

        keyboard_grabbed = false;
        xcb_ungrab_keyboard(base.x11_data.connection, XCB_TIME_CURRENT_TIME);
    }

    std::unique_ptr<platform_qobject> qobject;
    input::config config;

    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;

    input::xkb::manager<type> xkb;
    std::unique_ptr<global_shortcuts_manager> shortcuts;
    std::unique_ptr<dbus::device_manager<type>> dbus;

    Base& base;

    bool keyboard_grabbed{false};

private:
    bool are_key_syms_depressed(uint const keySyms[], int nKeySyms) const
    {
        struct KeySymbolsDeleter {
            static inline void cleanup(xcb_key_symbols_t* symbols)
            {
                xcb_key_symbols_free(symbols);
            }
        };

        base::x11::xcb::query_keymap keys(base.x11_data.connection);

        QScopedPointer<xcb_key_symbols_t, KeySymbolsDeleter> symbols(
            xcb_key_symbols_alloc(base.x11_data.connection));
        if (symbols.isNull() || !keys) {
            return false;
        }
        const auto keymap = keys->keys;

        bool depressed = false;
        for (int iKeySym = 0; iKeySym < nKeySyms; iKeySym++) {
            uint keySymX = keySyms[iKeySym];
            auto keyCodes = xcb_key_symbols_get_keycode(symbols.data(), keySymX);
            if (!keyCodes) {
                continue;
            }

            int j = 0;
            while (keyCodes[j] != XCB_NO_SYMBOL) {
                const xcb_keycode_t keyCodeX = keyCodes[j++];
                int i = keyCodeX / 8;
                char mask = 1 << (keyCodeX - (i * 8));

                if (i < 0 || i >= 32) {
                    continue;
                }

                qCDebug(KWIN_CORE) << iKeySym << ": keySymX=0x" << QString::number(keySymX, 16)
                                   << " i=" << i << " mask=0x" << QString::number(mask, 16)
                                   << " keymap[i]=0x" << QString::number(keymap[i], 16);

                if (keymap[i] & mask) {
                    depressed = true;
                    break;
                }
            }

            free(keyCodes);
        }

        return depressed;
    }
};

}
