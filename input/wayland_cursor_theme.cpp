/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wayland_cursor_theme.h"

#include "screens.h"
#include "wayland_server.h"

#include <QVector>

#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/output.h>

#include <wayland-cursor.h>

namespace KWin::input
{

wayland_cursor_theme::wayland_cursor_theme(Wrapland::Client::ShmPool* shm, QObject* parent)
    : QObject(parent)
    , m_theme(nullptr)
    , m_shm(shm)
{
    connect(screens(), &Screens::maxScaleChanged, this, &wayland_cursor_theme::loadTheme);
}

wayland_cursor_theme::~wayland_cursor_theme()
{
    destroyTheme();
}

void wayland_cursor_theme::loadTheme()
{
    if (!m_shm->isValid()) {
        return;
    }
    auto c = input::cursor::self();
    int size = c->themeSize();
    if (size == 0) {
        // set a default size
        size = 24;
    }

    size *= screens()->maxScale();

    auto theme = wl_cursor_theme_load(c->themeName().toUtf8().constData(), size, m_shm->shm());
    if (theme) {
        if (!m_theme) {
            // so far the theme had not been created, this means we need to start tracking theme
            // changes
            connect(c, &input::cursor::themeChanged, this, &wayland_cursor_theme::loadTheme);
        } else {
            destroyTheme();
        }
        m_theme = theme;
        emit themeChanged();
    }
}

void wayland_cursor_theme::destroyTheme()
{
    if (!m_theme) {
        return;
    }
    wl_cursor_theme_destroy(m_theme);
    m_theme = nullptr;
}

wl_cursor_image* wayland_cursor_theme::get(input::cursor_shape shape)
{
    return get(shape.name());
}

wl_cursor_image* wayland_cursor_theme::get(const QByteArray& name)
{
    if (!m_theme) {
        loadTheme();
    }
    if (!m_theme) {
        // loading cursor failed
        return nullptr;
    }
    wl_cursor* c = wl_cursor_theme_get_cursor(m_theme, name.constData());
    if (!c || c->image_count <= 0) {
        const auto& names = input::cursor::self()->cursorAlternativeNames(name);
        for (auto it = names.begin(), end = names.end(); it != end; it++) {
            c = wl_cursor_theme_get_cursor(m_theme, (*it).constData());
            if (c && c->image_count > 0) {
                break;
            }
        }
    }
    if (!c || c->image_count <= 0) {
        return nullptr;
    }
    // TODO: who deletes c?
    return c->images[0];
}

}
