/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_context_attribute_builder.h"
#include <wayland_logging.h>

#include <QOpenGLContext>
#include <epoxy/egl.h>
#include <memory>
#include <vector>

namespace KWin::render::gl
{

template<typename Backend>
EGLContext create_egl_context(Backend const& backend)
{
    auto const have_robustness
        = backend.hasExtension(QByteArrayLiteral("EGL_EXT_create_context_robustness"));
    auto const have_create_context
        = backend.hasExtension(QByteArrayLiteral("EGL_KHR_create_context"));
    const bool have_context_priority
        = backend.hasExtension(QByteArrayLiteral("EGL_IMG_context_priority"));

    std::vector<std::unique_ptr<context_attribute_builder>> candidates;

    if (have_create_context && have_robustness && have_context_priority) {
        auto glesRobustPriority = std::make_unique<egl_gles_context_attribute_builder>();
        glesRobustPriority->setVersion(2);
        glesRobustPriority->setRobust(true);
        glesRobustPriority->setHighPriority(true);
        candidates.push_back(std::move(glesRobustPriority));
    }
    if (have_create_context && have_robustness) {
        auto glesRobust = std::make_unique<egl_gles_context_attribute_builder>();
        glesRobust->setVersion(2);
        glesRobust->setRobust(true);
        candidates.push_back(std::move(glesRobust));
    }
    if (have_context_priority) {
        auto glesPriority = std::make_unique<egl_gles_context_attribute_builder>();
        glesPriority->setVersion(2);
        glesPriority->setHighPriority(true);
        candidates.push_back(std::move(glesPriority));
    }

    auto gles = std::make_unique<egl_gles_context_attribute_builder>();
    gles->setVersion(2);
    candidates.push_back(std::move(gles));

    auto ctx = EGL_NO_CONTEXT;
    for (auto& candidate : candidates) {
        auto const attribs = candidate->build();
        ctx = eglCreateContext(
            backend.data.base.display, backend.data.base.config, EGL_NO_CONTEXT, attribs.data());
        if (ctx != EGL_NO_CONTEXT) {
            qCDebug(KWIN_WL) << "Created EGL context with attributes:" << candidate.get();
            break;
        }
    }

    if (ctx == EGL_NO_CONTEXT) {
        qCCritical(KWIN_WL) << "Create Context failed";
        return ctx;
    }

    return ctx;
}

template<typename Backend>
bool init_egl_api(Backend& backend)
{
    EGLint major, minor;

    if (eglInitialize(backend.data.base.display, &major, &minor) == EGL_FALSE) {
        qCWarning(KWIN_WL) << "eglInitialize failed";
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            qCWarning(KWIN_WL) << "Error during eglInitialize " << error;
        }
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_WL) << "Error during eglInitialize " << error;
        return false;
    }

    qCDebug(KWIN_WL) << "Egl Initialize succeeded";

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        qCCritical(KWIN_WL) << "bind OpenGL API failed";
        return false;
    }

    qCDebug(KWIN_WL) << "EGL version: " << major << "." << minor;

    init_server_extensions(backend);
    return true;
}

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
