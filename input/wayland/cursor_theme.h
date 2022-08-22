/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"

#include "kwin_export.h"

#include <QObject>
#include <memory>

struct wl_cursor_image;
struct wl_cursor_theme;

namespace Wrapland::Client
{
class ShmPool;
}

namespace KWin::input::wayland
{

class cursor;

class KWIN_EXPORT cursor_theme_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void themeChanged();
};

class KWIN_EXPORT cursor_theme
{
public:
    cursor_theme(wayland::cursor& cursor, Wrapland::Client::ShmPool* shm);
    ~cursor_theme();

    wl_cursor_image* get(input::cursor_shape shape);
    wl_cursor_image* get(const QByteArray& name);

    std::unique_ptr<cursor_theme_qobject> qobject;

private:
    void loadTheme();
    void destroyTheme();

    wayland::cursor& cursor;
    wl_cursor_theme* m_theme{nullptr};
    Wrapland::Client::ShmPool* m_shm;
};

}
