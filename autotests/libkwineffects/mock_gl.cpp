/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mock_gl.h"

MockGL* s_gl = nullptr;

GLubyte const* mock_glGetString(GLenum name)
{
    if (!s_gl) {
        return nullptr;
    }
    switch (name) {
    case GL_VENDOR:
        return (const GLubyte*)s_gl->getString.vendor.constData();
    case GL_RENDERER:
        return (const GLubyte*)s_gl->getString.renderer.constData();
    case GL_VERSION:
        return (const GLubyte*)s_gl->getString.version.constData();
    case GL_EXTENSIONS:
        return (const GLubyte*)s_gl->getString.extensionsString.constData();
    case GL_SHADING_LANGUAGE_VERSION:
        return (const GLubyte*)s_gl->getString.shadingLanguageVersion.constData();
    default:
        return nullptr;
    }
}

GLubyte const* mock_glGetStringi(GLenum name, GLuint index)
{
    if (!s_gl) {
        return nullptr;
    }
    if (name == GL_EXTENSIONS && index < uint(s_gl->getString.extensions.count())) {
        return (const GLubyte*)s_gl->getString.extensions.at(index).constData();
    }
    return nullptr;
}

void mock_glGetIntegerv(GLenum pname, GLint* data)
{
    Q_UNUSED(pname)
    Q_UNUSED(data)
    if (pname == GL_NUM_EXTENSIONS) {
        if (data && s_gl) {
            *data = s_gl->getString.extensions.count();
        }
    }
}
