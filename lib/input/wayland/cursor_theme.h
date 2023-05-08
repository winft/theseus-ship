/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/platform.h"
#include "kwin_export.h"
#include "win/cursor_shape.h"

#include <QObject>
#include <Wrapland/Client/shm_pool.h>
#include <memory>
#include <wayland-cursor.h>

namespace KWin::input::wayland
{

class KWIN_EXPORT cursor_theme_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void themeChanged();
};

template<typename Cursor>
class cursor_theme
{
public:
    cursor_theme(Cursor& cursor, Wrapland::Client::ShmPool* shm)
        : qobject{std::make_unique<cursor_theme_qobject>()}
        , cursor{cursor}
        , m_shm{shm}
    {
        QObject::connect(&cursor.redirect.platform.base,
                         &base::platform::topology_changed,
                         qobject.get(),
                         [this](auto old, auto topo) {
                             if (old.max_scale != topo.max_scale) {
                                 loadTheme();
                             }
                         });
    }

    ~cursor_theme()
    {
        destroyTheme();
    }

    wl_cursor_image* get(win::cursor_shape shape)
    {
        return get(shape.name());
    }

    wl_cursor_image* get(const QByteArray& name)
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
            const auto& names = cursor.alternative_names(name);
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

    std::unique_ptr<cursor_theme_qobject> qobject;

private:
    void loadTheme()
    {
        if (!m_shm->isValid()) {
            return;
        }

        int size = cursor.theme_size();
        if (size == 0) {
            // set a default size
            size = 24;
        }

        size *= cursor.redirect.platform.base.topology.max_scale;

        auto theme
            = wl_cursor_theme_load(cursor.theme_name().toUtf8().constData(), size, m_shm->shm());
        if (theme) {
            if (!m_theme) {
                // so far the theme had not been created, this means we need to start tracking theme
                // changes
                QObject::connect(
                    &cursor, &Cursor::theme_changed, qobject.get(), [this] { loadTheme(); });
            } else {
                destroyTheme();
            }
            m_theme = theme;
            Q_EMIT qobject->themeChanged();
        }
    }

    void destroyTheme()
    {
        if (!m_theme) {
            return;
        }
        wl_cursor_theme_destroy(m_theme);
        m_theme = nullptr;
    }

    Cursor& cursor;
    wl_cursor_theme* m_theme{nullptr};
    Wrapland::Client::ShmPool* m_shm;
};

}
