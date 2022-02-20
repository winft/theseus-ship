/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_context_attribute_builder.h"
#include "wayland_logging.h"

#include <QOpenGLContext>
#include <epoxy/egl.h>
#include <memory>
#include <vector>

namespace KWin::render::gl
{

template<typename Backend>
void init_buffer_age(Backend& backend)
{
    backend.setSupportsBufferAge(false);

    if (backend.hasExtension(QByteArrayLiteral("EGL_EXT_buffer_age"))) {
        QByteArray const useBufferAge = qgetenv("KWIN_USE_BUFFER_AGE");

        if (useBufferAge != "0")
            backend.setSupportsBufferAge(true);
    }
}

template<typename Backend>
void init_server_extensions(Backend& backend)
{
    QByteArray const extensions = eglQueryString(backend.data.base.display, EGL_EXTENSIONS);
    backend.setExtensions(extensions.split(' '));
    backend.setSupportsSurfacelessContext(
        backend.hasExtension(QByteArrayLiteral("EGL_KHR_surfaceless_context")));
}

template<typename Backend>
void init_client_extensions(Backend& backend)
{
    // Get the list of client extensions
    char const* cstring = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    auto const extensions = QByteArray::fromRawData(cstring, qstrlen(cstring));

    if (extensions.isEmpty()) {
        // If eglQueryString() returned NULL, the implementation doesn't support
        // EGL_EXT_client_extensions. Expect an EGL_BAD_DISPLAY error.
        (void)eglGetError();
    }

    backend.data.base.client_extensions = extensions.split(' ');
}

}
