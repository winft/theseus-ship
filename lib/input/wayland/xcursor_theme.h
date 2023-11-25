/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QImage>
#include <QList>
#include <QSharedDataPointer>

#include <chrono>

namespace KWin::input::wayland
{

class xcursor_sprite_private;
class xcursor_theme_private;

/// The xcursor_sprite class represents a single sprite in the Xcursor theme.
class KWIN_EXPORT xcursor_sprite
{
public:
    xcursor_sprite();
    xcursor_sprite(xcursor_sprite const& other);
    xcursor_sprite(QImage const& data, QPoint const& hotspot, std::chrono::milliseconds delay);
    ~xcursor_sprite();

    xcursor_sprite& operator=(const xcursor_sprite& other);

    QImage data() const;

    /// (0,0) corresponds to the upper left corner. Coordinates are in device independent pixels.
    QPoint hotspot() const;

    /// Returns the time interval between this sprite and the next one, in milliseconds.
    std::chrono::milliseconds delay() const;

private:
    QSharedDataPointer<xcursor_sprite_private> d_ptr;
};

/// The xcursor_theme class represents an Xcursor theme.
class KWIN_EXPORT xcursor_theme
{
public:
    xcursor_theme();

    /// If no theme with the provided name exists, the cursor theme will be empty.
    xcursor_theme(QString const& theme, int size, double device_pixel_ratio);
    xcursor_theme(xcursor_theme const& other);

    ~xcursor_theme();

    xcursor_theme& operator=(xcursor_theme const& other);

    bool operator==(xcursor_theme const& other);
    bool operator!=(xcursor_theme const& other);

    bool empty() const;

    QList<xcursor_sprite> shape(QByteArray const& name) const;

private:
    QSharedDataPointer<xcursor_theme_private> d_ptr;
};

}
