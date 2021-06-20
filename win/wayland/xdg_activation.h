/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

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
constexpr size_t token_strlen{33};

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

struct xdg_activation {
    std::string token;

    void clear()
    {
        if (token.empty()) {
            return;
        }

        Q_EMIT effects->startupRemoved(QString::fromStdString(token));
        token.clear();
    }
};

template<typename Space>
void xdg_activation_create_token(Space* space, Wrapland::Server::XdgActivationTokenV1* token)
{
    auto check_allowance = [&] {
        if (!token->surface()) {
            qCDebug(KWIN_CORE) << "Token request has no surface set.";
            return false;
        }

        if (auto& plasma_surfaces = waylandServer()->m_plasmaShellSurfaces;
            std::any_of(plasma_surfaces.cbegin(),
                        plasma_surfaces.cend(),
                        [surface = token->surface()](auto const& plasma_surface) {
                            return plasma_surface->surface() == surface;
                        })) {
            // Plasma internal surfaces are always allowed.
            return true;
        }

        auto win = waylandServer()->find_window(token->surface());
        if (!win) {
            qCDebug(KWIN_CORE) << "No window associated with token surface" << token->surface();
            return false;
        }

        if (win->control && win->control->wayland_management()) {
            // Privileged windows are always allowed.
            return true;
        }

        if (win != space->active_client) {
            qCDebug(KWIN_CORE) << "Requesting window" << win << "currently not active.";
            return false;
        }
        return true;
    };

    if (!check_allowance()) {
        qCDebug(KWIN_CORE) << "Deny creation of XDG Activation token.";
        token->done("");
        return;
    }

    char token_str[token_strlen + 1] = {0};
    if (!generate_token(token_str)) {
        qCWarning(KWIN_CORE) << "Error creating XDG Activation token.";
        token->done("");
        return;
    }

    space->activation->clear();
    space->activation->token = token_str;

    token->done(token_str);

    if (!token->app_id().empty()) {
        auto const icon = QIcon::fromTheme(icon_from_desktop_file(QString(token->app_id().c_str())),
                                           QIcon::fromTheme(QStringLiteral("system-run")));
        Q_EMIT effects->startupAdded(token_str, icon);
    }
}

template<typename Space, typename Window>
void xdg_activation_activate(Space* space, Window* win, std::string const& token)
{
    assert(win);

    if (space->activation->token.empty()) {
        qCDebug(KWIN_CORE) << "Empty token provided on XDG Activation of" << win;
        set_demands_attention(win, true);
        return;
    }
    if (space->activation->token != token) {
        qCDebug(KWIN_CORE) << "Token mismatch on XDG Activation of" << win;
        qCDebug(KWIN_CORE).nospace() << "Provided: '" << token.c_str() << "', match: '"
                                     << space->activation->token.c_str() << "'";
        set_demands_attention(win, true);
        return;
    }

    space->activation->clear();
    space->activateClient(win);
}

}
