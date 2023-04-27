/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "extras.h"

#include "net/root_info.h"
#include "net/win_info.h"

#include "base/logging.h"

#include <QBitmap>
#include <QGuiApplication>
#include <QIcon>
#include <QRect>
#include <QScreen>
#include <iostream>
#include <private/qtx11extras_p.h>

namespace KWin::win::x11
{

// QPoint and QSize all have handy / operators which are useful for scaling, positions and sizes for
// high DPI support QRect does not, so we create one for internal purposes within this class
inline QRect operator/(const QRect& rectangle, qreal factor)
{
    return QRect(rectangle.topLeft() / factor, rectangle.size() / factor);
}

struct CDeleter {
    template<typename T>
    void operator()(T* ptr)
    {
        free(ptr);
    }
};
template<typename T>
using UniqueCPointer = std::unique_ptr<T, CDeleter>;

template<typename T>
T from_native_pixmap(xcb_pixmap_t pixmap, xcb_connection_t* c)
{
    const xcb_get_geometry_cookie_t geoCookie = xcb_get_geometry_unchecked(c, pixmap);
    UniqueCPointer<xcb_get_geometry_reply_t> geo(xcb_get_geometry_reply(c, geoCookie, nullptr));
    if (!geo) {
        // getting geometry for the pixmap failed
        return T();
    }

    const xcb_get_image_cookie_t imageCookie = xcb_get_image_unchecked(
        c, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0, geo->width, geo->height, ~0);
    UniqueCPointer<xcb_get_image_reply_t> xImage(xcb_get_image_reply(c, imageCookie, nullptr));
    if (!xImage) {
        // request for image data failed
        return T();
    }
    QImage::Format format = QImage::Format_Invalid;
    switch (xImage->depth) {
    case 1:
        format = QImage::Format_MonoLSB;
        break;
    case 16:
        format = QImage::Format_RGB16;
        break;
    case 24:
        format = QImage::Format_RGB32;
        break;
    case 30: {
        // Qt doesn't have a matching image format. We need to convert manually
        uint32_t* pixels = reinterpret_cast<uint32_t*>(xcb_get_image_data(xImage.get()));
        for (uint i = 0; i < xImage.get()->length; ++i) {
            int r = (pixels[i] >> 22) & 0xff;
            int g = (pixels[i] >> 12) & 0xff;
            int b = (pixels[i] >> 2) & 0xff;

            pixels[i] = qRgba(r, g, b, 0xff);
        }
        // fall through, Qt format is still Format_ARGB32_Premultiplied
        Q_FALLTHROUGH();
    }
    case 32:
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    default:
        return T(); // we don't know
    }
    QImage image(xcb_get_image_data(xImage.get()),
                 geo->width,
                 geo->height,
                 xcb_get_image_data_length(xImage.get()) / geo->height,
                 format,
                 free,
                 xImage.get());
    xImage.release();
    if (image.isNull()) {
        return T();
    }
    if (image.format() == QImage::Format_MonoLSB) {
        // work around an abort in QImage::color
        image.setColorCount(2);
        image.setColor(0, QColor(Qt::white).rgb());
        image.setColor(1, QColor(Qt::black).rgb());
    }
    return T::fromImage(image);
}

QPixmap createPixmapFromHandle(xcb_connection_t* c, WId pixmap, WId pixmap_mask)
{
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    std::cout << "Byte order not supported" << std::endl;
    return QPixmap();
#endif
    const xcb_setup_t* setup = xcb_get_setup(c);
    if (setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) {
        std::cout << "Byte order not supported" << std::endl;
        return QPixmap();
    }

    auto pix = from_native_pixmap<QPixmap>(pixmap, c);

    if (pixmap_mask != XCB_PIXMAP_NONE) {
        auto mask = from_native_pixmap<QBitmap>(pixmap_mask, c);
        if (mask.size() != pix.size()) {
            return QPixmap();
        }
        pix.setMask(mask);
    }

    return pix;
}

QPixmap iconFromNetWinInfo(int width, int height, bool scale, int flags, net::win_info const& info)
{
    QPixmap result;

    if (flags & extras::NETWM) {
        auto ni = info.icon(width, height);
        if (ni.data && ni.size.width > 0 && ni.size.height > 0) {
            QImage img(
                (uchar*)ni.data, (int)ni.size.width, (int)ni.size.height, QImage::Format_ARGB32);
            if (scale && width > 0 && height > 0 && img.size() != QSize(width, height)
                && !img.isNull()) {
                img = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            if (!img.isNull()) {
                result = QPixmap::fromImage(img);
            }
            return result;
        }
    }

    if (flags & extras::WMHints) {
        xcb_pixmap_t p = info.icccmIconPixmap();
        xcb_pixmap_t p_mask = info.icccmIconPixmapMask();

        if (p != XCB_PIXMAP_NONE) {
            auto pm = createPixmapFromHandle(info.xcbConnection(), p, p_mask);
            if (scale && width > 0 && height > 0 && !pm.isNull() //
                && (pm.width() != width || pm.height() != height)) {
                result = QPixmap::fromImage(pm.toImage().scaled(
                    width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            } else {
                result = pm;
            }
        }
    }

    // Since width can be any arbitrary size, but the icons cannot,
    // take the nearest value for best results (ignoring 22 pixel
    // icons as they don't exist for apps):
    int iconWidth;
    if (width < 24) {
        iconWidth = 16;
    } else if (width < 40) {
        iconWidth = 32;
    } else if (width < 56) {
        iconWidth = 48;
    } else if (width < 96) {
        iconWidth = 64;
    } else if (width < 192) {
        iconWidth = 128;
    } else {
        iconWidth = 256;
    }

    if (flags & extras::ClassHint) {
        // Try to load the icon from the classhint if the app didn't specify
        // its own:
        if (result.isNull()) {
            const QIcon icon
                = QIcon::fromTheme(QString::fromUtf8(info.windowClassClass()).toLower());
            const QPixmap pm = icon.isNull() ? QPixmap() : icon.pixmap(iconWidth, iconWidth);
            if (scale && !pm.isNull()) {
                result = QPixmap::fromImage(pm.toImage().scaled(
                    width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            } else {
                result = pm;
            }
        }
    }

    if (flags & extras::XApp) {
        // If the icon is still a null pixmap, load the icon for X applications
        // as a last resort:
        if (result.isNull()) {
            const QIcon icon = QIcon::fromTheme(QStringLiteral("xorg"));
            const QPixmap pm = icon.isNull() ? QPixmap() : icon.pixmap(iconWidth, iconWidth);
            if (scale && !pm.isNull()) {
                result = QPixmap::fromImage(pm.toImage().scaled(
                    width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            } else {
                result = pm;
            }
        }
    }
    return result;
}

QPixmap extras::icon(net::win_info const& info, int width, int height, bool scale, int flags)
{
    // TODO(romangg): Get dpr internally instead.
    width *= qGuiApp->devicePixelRatio();
    height *= qGuiApp->devicePixelRatio();

    return iconFromNetWinInfo(width, height, scale, flags, info);
}

}
