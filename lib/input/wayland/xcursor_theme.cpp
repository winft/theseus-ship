/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xcursor_theme.h"

#include <input/extern/xcursor.h>

#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QSharedData>
#include <QStack>
#include <QStandardPaths>

namespace KWin::input::wayland
{

class xcursor_sprite_private : public QSharedData
{
public:
    QImage data;
    QPoint hotspot;
    std::chrono::milliseconds delay;
};

class xcursor_theme_private : public QSharedData
{
public:
    void load(QString const& name, int size, double device_pixel_ratio);
    void load_cursors(QString const& package_path, int size, double device_pixel_ratio);

    QHash<QByteArray, QList<xcursor_sprite>> registry;
};

xcursor_sprite::xcursor_sprite()
    : d_ptr{new xcursor_sprite_private}
{
}

xcursor_sprite::xcursor_sprite(xcursor_sprite const& other)
    : d_ptr{other.d_ptr}
{
}

xcursor_sprite::~xcursor_sprite() = default;

xcursor_sprite& xcursor_sprite::operator=(xcursor_sprite const& other)
{
    d_ptr = other.d_ptr;
    return *this;
}

xcursor_sprite::xcursor_sprite(QImage const& data,
                               QPoint const& hotspot,
                               std::chrono::milliseconds delay)
    : d_ptr(new xcursor_sprite_private)
{
    d_ptr->data = data;
    d_ptr->hotspot = hotspot;
    d_ptr->delay = delay;
}

QImage xcursor_sprite::data() const
{
    return d_ptr->data;
}

QPoint xcursor_sprite::hotspot() const
{
    return d_ptr->hotspot;
}

std::chrono::milliseconds xcursor_sprite::delay() const
{
    return d_ptr->delay;
}

static QList<xcursor_sprite>
load_cursor(QString const& file_path, int target_size, double device_pixel_ratio)
{
    auto images
        = XcursorFileLoadImages(QFile::encodeName(file_path), target_size * device_pixel_ratio);
    if (!images) {
        return {};
    }

    QList<xcursor_sprite> sprites;

    for (int i = 0; i < images->nimage; ++i) {
        auto const* nativeCursorImage = images->images[i];
        auto const scale = std::max(1., static_cast<double>(nativeCursorImage->size) / target_size);
        QPoint const hotspot(nativeCursorImage->xhot, nativeCursorImage->yhot);
        std::chrono::milliseconds const delay(nativeCursorImage->delay);

        QImage data(nativeCursorImage->width,
                    nativeCursorImage->height,
                    QImage::Format_ARGB32_Premultiplied);
        data.setDevicePixelRatio(scale);
        memcpy(data.bits(), nativeCursorImage->pixels, data.sizeInBytes());

        sprites.append(xcursor_sprite(data, hotspot / scale, delay));
    }

    XcursorImagesDestroy(images);
    return sprites;
}

void xcursor_theme_private::load_cursors(QString const& package_path,
                                         int size,
                                         double device_pixel_ratio)
{
    QDir const dir(package_path);
    auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    std::partition(
        entries.begin(), entries.end(), [](auto const& info) { return !info.isSymLink(); });

    for (auto const& entry : std::as_const(entries)) {
        auto const shape = QFile::encodeName(entry.fileName());
        if (registry.contains(shape)) {
            continue;
        }
        if (entry.isSymLink()) {
            QFileInfo const symLinkInfo(entry.symLinkTarget());
            if (symLinkInfo.absolutePath() == entry.absolutePath()) {
                auto const sprites = registry.value(QFile::encodeName(symLinkInfo.fileName()));
                if (!sprites.isEmpty()) {
                    registry.insert(shape, sprites);
                    continue;
                }
            }
        }
        auto const sprites = load_cursor(entry.absoluteFilePath(), size, device_pixel_ratio);
        if (!sprites.isEmpty()) {
            registry.insert(shape, sprites);
        }
    }
}

static QStringList search_paths()
{
    static QStringList paths;
    if (!paths.isEmpty()) {
        return paths;
    }

    if (auto const env = qEnvironmentVariable("XCURSOR_PATH"); !env.isEmpty()) {
        paths.append(env.split(':', Qt::SkipEmptyParts));
    } else {
        auto const home = QDir::homePath();
        if (!home.isEmpty()) {
            paths.append(home + QLatin1String("/.icons"));
        }
        auto const data_dirs
            = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (auto const& dir : data_dirs) {
            paths.append(dir + QLatin1String("/icons"));
        }
    }

    return paths;
}

void xcursor_theme_private::load(QString const& name, int size, double device_pixel_ratio)
{
    auto const paths = search_paths();
    bool default_fallback = false;

    QSet<QString> loaded;
    QStack<QString> stack;
    stack.push(name);

    while (!stack.isEmpty()) {
        auto const name = stack.pop();
        if (loaded.contains(name)) {
            continue;
        }

        QStringList inherits;

        for (auto const& path : paths) {
            QDir const dir(path + QLatin1Char('/') + name);
            if (!dir.exists()) {
                continue;
            }
            load_cursors(dir.filePath(QStringLiteral("cursors")), size, device_pixel_ratio);
            if (inherits.isEmpty()) {
                KConfig const config(dir.filePath(QStringLiteral("index.theme")),
                                     KConfig::NoGlobals);
                inherits
                    << KConfigGroup(&config, "Icon Theme").readEntry("Inherits", QStringList());
            }
        }

        loaded.insert(name);
        for (auto it = inherits.crbegin(); it != inherits.crend(); ++it) {
            stack.push(*it);
        }

        if (registry.empty() && name == "default" && !default_fallback) {
            // This is a last resort in case we haven't found any theme directly in a "cursors"
            // directory, through inherit of index.theme in standard paths or XCURSOR_PATH.
            // We aim for always having a theme because otherwise no cursor is painted.
            default_fallback = true;
            stack.push("Adwaita");
            stack.push("breeze_cursors");
        }
    }
}

xcursor_theme::xcursor_theme()
    : d_ptr{new xcursor_theme_private}
{
}

xcursor_theme::xcursor_theme(QString const& name, int size, double device_pixel_ratio)
    : d_ptr{new xcursor_theme_private}
{
    d_ptr->load(name, size, device_pixel_ratio);
}

xcursor_theme::xcursor_theme(xcursor_theme const& other)
    : d_ptr{other.d_ptr}
{
}

xcursor_theme::~xcursor_theme()
{
}

xcursor_theme& xcursor_theme::operator=(xcursor_theme const& other)
{
    d_ptr = other.d_ptr;
    return *this;
}

bool xcursor_theme::operator==(xcursor_theme const& other)
{
    return d_ptr == other.d_ptr;
}

bool xcursor_theme::operator!=(xcursor_theme const& other)
{
    return !(*this == other);
}

bool xcursor_theme::empty() const
{
    return d_ptr->registry.isEmpty();
}

QList<xcursor_sprite> xcursor_theme::shape(QByteArray const& name) const
{
    return d_ptr->registry.value(name);
}

}
