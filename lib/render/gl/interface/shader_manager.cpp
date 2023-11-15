/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "shader_manager.h"

#include "platform.h"

#include <base/logging.h>
#include <render/effect/interface/paint_data.h>
#include <render/effect/interface/types.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/vertex_buffer.h>

#include <QFile>

namespace KWin
{

ShaderManager* ShaderManager::s_shaderManager = nullptr;

ShaderManager* ShaderManager::instance()
{
    if (!s_shaderManager) {
        s_shaderManager = new ShaderManager();
    }
    return s_shaderManager;
}

void ShaderManager::cleanup()
{
    delete s_shaderManager;
    s_shaderManager = nullptr;
}

ShaderManager::ShaderManager()
{
}

ShaderManager::~ShaderManager()
{
    while (!m_boundShaders.empty()) {
        popShader();
    }
}

QByteArray ShaderManager::generateVertexSource(ShaderTraits traits) const
{
    QByteArray source;
    QTextStream stream(&source);

    GLPlatform* const gl = GLPlatform::instance();
    QByteArray attribute, varying;

    if (!gl->isGLES()) {
        const bool glsl_140 = gl->glslVersion() >= kVersionNumber(1, 40);

        attribute = glsl_140 ? QByteArrayLiteral("in") : QByteArrayLiteral("attribute");
        varying = glsl_140 ? QByteArrayLiteral("out") : QByteArrayLiteral("varying");

        if (glsl_140)
            stream << "#version 140\n\n";
    } else {
        const bool glsl_es_300 = gl->glslVersion() >= kVersionNumber(3, 0);

        attribute = glsl_es_300 ? QByteArrayLiteral("in") : QByteArrayLiteral("attribute");
        varying = glsl_es_300 ? QByteArrayLiteral("out") : QByteArrayLiteral("varying");

        if (glsl_es_300)
            stream << "#version 300 es\n\n";
    }

    stream << attribute << " vec4 position;\n";
    if (traits & ShaderTrait::MapTexture) {
        stream << attribute << " vec4 texcoord;\n\n";
        stream << varying << " vec2 texcoord0;\n\n";
    } else
        stream << "\n";

    stream << "uniform mat4 modelViewProjectionMatrix;\n\n";

    stream << "void main()\n{\n";
    if (traits & ShaderTrait::MapTexture)
        stream << "    texcoord0 = texcoord.st;\n";

    stream << "    gl_Position = modelViewProjectionMatrix * position;\n";
    stream << "}\n";

    stream.flush();
    return source;
}

QByteArray ShaderManager::generateFragmentSource(ShaderTraits traits) const
{
    QByteArray source;
    QTextStream stream(&source);

    GLPlatform* const gl = GLPlatform::instance();
    QByteArray varying, output, textureLookup;

    if (!gl->isGLES()) {
        const bool glsl_140 = gl->glslVersion() >= kVersionNumber(1, 40);

        if (glsl_140)
            stream << "#version 140\n\n";

        varying = glsl_140 ? QByteArrayLiteral("in") : QByteArrayLiteral("varying");
        textureLookup = glsl_140 ? QByteArrayLiteral("texture") : QByteArrayLiteral("texture2D");
        output = glsl_140 ? QByteArrayLiteral("fragColor") : QByteArrayLiteral("gl_FragColor");
    } else {
        const bool glsl_es_300 = GLPlatform::instance()->glslVersion() >= kVersionNumber(3, 0);

        if (glsl_es_300)
            stream << "#version 300 es\n\n";

        // From the GLSL ES specification:
        //
        //     "The fragment language has no default precision qualifier for floating point types."
        stream << "precision highp float;\n\n";

        varying = glsl_es_300 ? QByteArrayLiteral("in") : QByteArrayLiteral("varying");
        textureLookup = glsl_es_300 ? QByteArrayLiteral("texture") : QByteArrayLiteral("texture2D");
        output = glsl_es_300 ? QByteArrayLiteral("fragColor") : QByteArrayLiteral("gl_FragColor");
    }

    if (traits & ShaderTrait::MapTexture) {
        stream << "uniform sampler2D sampler;\n";

        if (traits & ShaderTrait::Modulate)
            stream << "uniform vec4 modulation;\n";
        if (traits & ShaderTrait::AdjustSaturation)
            stream << "uniform float saturation;\n";

        stream << "\n" << varying << " vec2 texcoord0;\n";

    } else if (traits & ShaderTrait::UniformColor)
        stream << "uniform vec4 geometryColor;\n";

    if (output != QByteArrayLiteral("gl_FragColor"))
        stream << "\nout vec4 " << output << ";\n";

    stream << "\nvoid main(void)\n{\n";
    if (traits & ShaderTrait::MapTexture) {
        stream << "vec2 texcoordC = texcoord0;\n";

        if (traits & (ShaderTrait::Modulate | ShaderTrait::AdjustSaturation)) {
            stream << "    vec4 texel = " << textureLookup << "(sampler, texcoordC);\n";
            if (traits & ShaderTrait::Modulate)
                stream << "    texel *= modulation;\n";
            if (traits & ShaderTrait::AdjustSaturation)
                stream << "    texel.rgb = mix(vec3(dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722))), "
                          "texel.rgb, saturation);\n";

            stream << "    " << output << " = texel;\n";
        } else {
            stream << "    " << output << " = " << textureLookup << "(sampler, texcoordC);\n";
        }
    } else if (traits & ShaderTrait::UniformColor)
        stream << "    " << output << " = geometryColor;\n";

    stream << "}";
    stream.flush();
    return source;
}

std::unique_ptr<GLShader> ShaderManager::generateShader(ShaderTraits traits)
{
    return generateCustomShader(traits);
}

std::unique_ptr<GLShader> ShaderManager::generateCustomShader(ShaderTraits traits,
                                                              const QByteArray& vertexSource,
                                                              const QByteArray& fragmentSource)
{
    const QByteArray vertex = vertexSource.isEmpty() ? generateVertexSource(traits) : vertexSource;
    const QByteArray fragment
        = fragmentSource.isEmpty() ? generateFragmentSource(traits) : fragmentSource;

#if 0
    qCDebug(KWIN_CORE) << "**************";
    qCDebug(KWIN_CORE) << vertex;
    qCDebug(KWIN_CORE) << "**************";
    qCDebug(KWIN_CORE) << fragment;
    qCDebug(KWIN_CORE) << "**************";
#endif

    std::unique_ptr<GLShader> shader{new GLShader(GLShader::ExplicitLinking)};
    shader->load(vertex, fragment);

    shader->bindAttributeLocation("position", VA_Position);
    shader->bindAttributeLocation("texcoord", VA_TexCoord);
    shader->bindFragDataLocation("fragColor", 0);

    shader->link();
    return shader;
}

static QString resolveShaderFilePath(const QString& filePath)
{
    QString suffix;
    QString extension;

    const qint64 coreVersionNumber
        = GLPlatform::instance()->isGLES() ? kVersionNumber(3, 0) : kVersionNumber(1, 40);
    if (GLPlatform::instance()->glslVersion() >= coreVersionNumber) {
        suffix = QStringLiteral("_core");
    }

    if (filePath.endsWith(QStringLiteral(".frag"))) {
        extension = QStringLiteral(".frag");
    } else if (filePath.endsWith(QStringLiteral(".vert"))) {
        extension = QStringLiteral(".vert");
    } else {
        qCWarning(KWIN_CORE) << filePath << "must end either with .vert or .frag";
        return QString();
    }

    const QString prefix = filePath.chopped(extension.size());
    return prefix + suffix + extension;
}

std::unique_ptr<GLShader> ShaderManager::generateShaderFromFile(ShaderTraits traits,
                                                                const QString& vertexFile,
                                                                const QString& fragmentFile)
{
    auto loadShaderFile = [](const QString& filePath) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            return file.readAll();
        }
        qCCritical(KWIN_CORE) << "Failed to read shader " << filePath;
        return QByteArray();
    };
    QByteArray vertexSource;
    QByteArray fragmentSource;
    if (!vertexFile.isEmpty()) {
        vertexSource = loadShaderFile(resolveShaderFilePath(vertexFile));
        if (vertexSource.isEmpty()) {
            return std::unique_ptr<GLShader>(new GLShader());
        }
    }
    if (!fragmentFile.isEmpty()) {
        fragmentSource = loadShaderFile(resolveShaderFilePath(fragmentFile));
        if (fragmentSource.isEmpty()) {
            return std::unique_ptr<GLShader>(new GLShader());
        }
    }
    return generateCustomShader(traits, vertexSource, fragmentSource);
}

GLShader* ShaderManager::shader(ShaderTraits traits)
{
    std::unique_ptr<GLShader>& shader = m_shaderHash[traits];

    if (!shader) {
        shader = generateShader(traits);
    }

    return shader.get();
}

GLShader* ShaderManager::getBoundShader() const
{
    if (m_boundShaders.empty()) {
        return nullptr;
    }
    return m_boundShaders.top();
}

bool ShaderManager::isShaderBound() const
{
    return !m_boundShaders.empty();
}

GLShader* ShaderManager::pushShader(ShaderTraits traits)
{
    GLShader* shader = this->shader(traits);
    pushShader(shader);
    return shader;
}

void ShaderManager::pushShader(GLShader* shader)
{
    // only bind shader if it is not already bound
    if (shader != getBoundShader()) {
        shader->bind();
    }
    m_boundShaders.push(shader);
}

void ShaderManager::popShader()
{
    if (m_boundShaders.empty()) {
        return;
    }

    auto shader = m_boundShaders.top();
    m_boundShaders.pop();

    if (m_boundShaders.empty()) {
        // no more shader bound - unbind
        shader->unbind();
    } else if (shader != m_boundShaders.top()) {
        // only rebind if a different shader is on top of stack
        m_boundShaders.top()->bind();
    }
}

void ShaderManager::bindFragDataLocations(GLShader* shader)
{
    shader->bindFragDataLocation("fragColor", 0);
}

void ShaderManager::bindAttributeLocations(GLShader* shader) const
{
    shader->bindAttributeLocation("vertex", VA_Position);
    shader->bindAttributeLocation("texCoord", VA_TexCoord);
}

std::unique_ptr<GLShader> ShaderManager::loadShaderFromCode(const QByteArray& vertexSource,
                                                            const QByteArray& fragmentSource)
{
    std::unique_ptr<GLShader> shader{new GLShader(GLShader::ExplicitLinking)};
    shader->load(vertexSource, fragmentSource);
    bindAttributeLocations(shader.get());
    bindFragDataLocations(shader.get());
    shader->link();
    return shader;
}

}
