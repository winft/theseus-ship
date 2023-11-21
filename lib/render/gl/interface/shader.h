/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QByteArray>
#include <QString>
#include <epoxy/gl.h>

class QColor;
class QVector2D;
class QVector3D;
class QVector4D;
class QMatrix4x4;

namespace KWin
{

class KWIN_EXPORT GLShader
{
public:
    enum Flags {
        NoFlags = 0,
        ExplicitLinking = (1 << 0),
    };

    GLShader(const QString& vertexfile, const QString& fragmentfile, unsigned int flags = NoFlags);
    ~GLShader();

    bool isValid() const
    {
        return mValid;
    }

    void bindAttributeLocation(const char* name, int index);
    void bindFragDataLocation(const char* name, int index);

    bool link();

    int uniformLocation(const char* name);

    bool setUniform(const char* name, float value);
    bool setUniform(const char* name, int value);
    bool setUniform(const char* name, const QVector2D& value);
    bool setUniform(const char* name, const QVector3D& value);
    bool setUniform(const char* name, const QVector4D& value);
    bool setUniform(const char* name, const QMatrix4x4& value);
    bool setUniform(const char* name, const QColor& color);

    bool setUniform(int location, float value);
    bool setUniform(int location, int value);
    bool setUniform(int location, const QVector2D& value);
    bool setUniform(int location, const QVector3D& value);
    bool setUniform(int location, const QVector4D& value);
    bool setUniform(int location, const QMatrix4x4& value);
    bool setUniform(int location, const QColor& value);

    int attributeLocation(const char* name);
    bool setAttribute(const char* name, float value);
    /**
     * @return The value of the uniform as a matrix
     * @since 4.7
     */
    QMatrix4x4 getUniformMatrix4x4(const char* name);

    enum MatrixUniform {
        ModelViewProjectionMatrix,
        MatrixCount,
    };

    enum Vec2Uniform { Offset, Vec2UniformCount };

    enum Vec4Uniform { ModulationConstant, Vec4UniformCount };

    enum FloatUniform { Saturation, FloatUniformCount };

    enum IntUniform {
        AlphaToOne, ///< @deprecated no longer used
        TextureWidth,
        TextureHeight,
        IntUniformCount
    };

    enum ColorUniform { Color, ColorUniformCount };

    bool setUniform(MatrixUniform uniform, const QMatrix4x4& matrix);
    bool setUniform(Vec2Uniform uniform, const QVector2D& value);
    bool setUniform(Vec4Uniform uniform, const QVector4D& value);
    bool setUniform(FloatUniform uniform, float value);
    bool setUniform(IntUniform uniform, int value);
    bool setUniform(ColorUniform uniform, const QVector4D& value);
    bool setUniform(ColorUniform uniform, const QColor& value);

protected:
    GLShader(unsigned int flags = NoFlags);
    bool loadFromFiles(const QString& vertexfile, const QString& fragmentfile);
    bool load(const QByteArray& vertexSource, const QByteArray& fragmentSource);
    const QByteArray prepareSource(GLenum shaderType, const QByteArray& sourceCode) const;
    bool compile(GLuint program, GLenum shaderType, const QByteArray& sourceCode) const;
    void bind();
    void unbind();
    void resolveLocations();

private:
    unsigned int mProgram;
    bool mValid : 1;
    bool mLocationsResolved : 1;
    bool mExplicitLinking : 1;
    int mMatrixLocation[MatrixCount];
    int mVec2Location[Vec2UniformCount];
    int mVec4Location[Vec4UniformCount];
    int mFloatLocation[FloatUniformCount];
    int mIntLocation[IntUniformCount];
    int mColorLocation[ColorUniformCount];

    friend class ShaderManager;
};

}
