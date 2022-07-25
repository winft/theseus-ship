/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"

#include "kwin_export.h"

#include <QObject>

struct wl_cursor_image;
struct wl_cursor_theme;

namespace Wrapland::Client
{
class ShmPool;
}

namespace KWin::input::wayland
{

class cursor;

// Exported for integration tests.
class KWIN_EXPORT cursor_theme : public QObject
{
    Q_OBJECT
public:
    cursor_theme(wayland::cursor& cursor, Wrapland::Client::ShmPool* shm);
    ~cursor_theme() override;

    wl_cursor_image* get(input::cursor_shape shape);
    wl_cursor_image* get(const QByteArray& name);

Q_SIGNALS:
    void themeChanged();

private:
    void loadTheme();
    void destroyTheme();

    wayland::cursor& cursor;
    wl_cursor_theme* m_theme{nullptr};
    Wrapland::Client::ShmPool* m_shm;
};

}
