/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef MOCK_GL_H
#define MOCK_GL_H

#include <QByteArray>
#include <QVector>
#include <epoxy/gl.h>

struct MockGL {
    struct {
        QByteArray vendor;
        QByteArray renderer;
        QByteArray version;
        QVector<QByteArray> extensions;
        QByteArray extensionsString;
        QByteArray shadingLanguageVersion;
    } getString;
};

extern MockGL* s_gl;

GLubyte const* mock_glGetString(GLenum name);
GLubyte const* mock_glGetStringi(GLenum name, GLuint index);
void mock_glGetIntegerv(GLenum pname, GLint* data);

#endif
