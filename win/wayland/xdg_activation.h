/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/logging.h"
#include "base/wayland/server.h"
#include "wayland_logging.h"
#include "win/activation.h"
#include "win/stacking.h"

#include <Wrapland/Server/xdg_activation_v1.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace KWin::win::wayland
{

namespace
{
// From wlroots' util/token.
inline constexpr size_t token_strlen{33};

inline bool generate_token(char out[token_strlen])
{
    static FILE* urandom = NULL;
    uint64_t data[2];

    if (!urandom) {
        if (!(urandom = fopen("/dev/urandom", "r"))) {
            qCWarning(KWIN_CORE) << "Failed to open random device.";
            return false;
        }
    }
    if (fread(data, sizeof(data), 1, urandom) != 1) {
        qCWarning(KWIN_CORE) << "Failed to read from random device.";
        return false;
    }
    if (snprintf(out, token_strlen, "%016" PRIx64 "%016" PRIx64, data[0], data[1])
        != token_strlen - 1) {
        qCWarning(KWIN_CORE) << "Failed to format hex string token.";
        return false;
    }
    return true;
}
}

template<typename Space>
struct xdg_activation {
    xdg_activation(Space& space)
        : interface {
        space.base.server->display->createXdgActivationV1()
    }, space{space}
    {
        QObject::connect(
            interface.get(),
            &Wrapland::Server::XdgActivationV1::token_requested,
            space.qobject.get(),
            [this](auto token) { xdg_activation_handle_token_request(this->space, *token); });
        QObject::connect(interface.get(),
                         &Wrapland::Server::XdgActivationV1::activate,
                         space.qobject.get(),
                         [this](auto const& token, auto surface) {
                             handle_xdg_activation_activate(&this->space, token, surface);
                         });
    }

    void clear()
    {
        if (token.empty()) {
            return;
        }

        if (!appid.empty()) {
            space.plasma_activation_feedback->finished(appid);
            appid.clear();
        }

        Q_EMIT space.base.render->compositor->effects->startupRemoved(
            QString::fromStdString(token));
        token.clear();
    }

    std::string token;
    std::string appid;

    std::unique_ptr<Wrapland::Server::XdgActivationV1> interface;

private:
    Space& space;
};

template<typename Space>
std::string xdg_activation_set_token(Space& space, std::string const& appid)
{
    char token_str[token_strlen + 1] = {0};
    if (!generate_token(token_str)) {
        qCWarning(KWIN_CORE) << "Error creating XDG Activation token.";
        return {};
    }

    space.xdg_activation->clear();
    space.xdg_activation->token = token_str;
    space.xdg_activation->appid = appid;

    if (!appid.empty()) {
        space.plasma_activation_feedback->app_id(appid);
        auto const icon = QIcon::fromTheme(icon_from_desktop_file(QString::fromStdString(appid)),
                                           QIcon::fromTheme(QStringLiteral("system-run")));
        Q_EMIT space.base.render->compositor->effects->startupAdded(token_str, icon);
    }
    return token_str;
}

template<typename Space, typename TokenRequest>
void xdg_activation_handle_token_request(Space& space, TokenRequest& token)
{
    auto check_allowance = [&] {
        if (!token.surface()) {
            qCDebug(KWIN_CORE) << "Token request has no surface set.";
            return false;
        }

        if (auto& plasma_surfaces = space.plasma_shell_surfaces;
            std::any_of(plasma_surfaces.cbegin(),
                        plasma_surfaces.cend(),
                        [surface = token.surface()](auto const& plasma_surface) {
                            return plasma_surface->surface() == surface;
                        })) {
            // Plasma internal surfaces are always allowed.
            return true;
        }

        auto win = space.find_window(token.surface());
        if (!win) {
            qCDebug(KWIN_CORE) << "No window associated with token surface" << token.surface();
            return false;
        }

        if (win != space.stacking.active) {
            qCDebug(KWIN_CORE) << "Requesting window" << win << "currently not active.";
            return false;
        }
        return true;
    };

    if (!check_allowance()) {
        qCDebug(KWIN_CORE) << "Deny creation of XDG Activation token.";
        token.done("");
        return;
    }

    token.done(xdg_activation_set_token(space, token.app_id()));
}

template<typename Space, typename Window>
void xdg_activation_activate(Space* space, Window* win, std::string const& token)
{
    assert(win);

    if (space->xdg_activation->token.empty()) {
        qCDebug(KWIN_CORE) << "Empty token provided on XDG Activation of" << win;
        set_demands_attention(win, true);
        return;
    }
    if (space->xdg_activation->token != token) {
        qCDebug(KWIN_CORE) << "Token mismatch on XDG Activation of" << win;
        qCDebug(KWIN_CORE).nospace() << "Provided: '" << token.c_str() << "', match: '"
                                     << space->xdg_activation->token.c_str() << "'";
        set_demands_attention(win, true);
        return;
    }

    space->xdg_activation->clear();
    activate_window(*space, *win);
}

template<typename Space>
void handle_xdg_activation_activate(Space* space,
                                    std::string const& token,
                                    Wrapland::Server::Surface* surface)
{
    auto win = space->find_window(surface);
    if (!win) {
        qCDebug(KWIN_CORE) << "No window found to xdg-activate" << surface;
        return;
    }

    while (!win->control) {
        auto lead = win->transient->lead();
        if (!lead) {
            qCDebug(KWIN_CORE) << "No window lead with control found to xdg-activate" << surface;
            return;
        }
        win = static_cast<decltype(win)>(lead);
    }

    xdg_activation_activate(space, win, token);
}

}
