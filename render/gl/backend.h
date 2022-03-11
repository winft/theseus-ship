/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QRegion>

#include <kwin_export.h>

namespace KWin
{
namespace base
{
class output;
}

class SceneOpenGL;

namespace render::gl
{

class texture;
class texture_private;

/**
 * @brief The backend creates and holds the OpenGL context and is responsible for Texture from
 * Pixmap.
 *
 * The backend is an abstract base class used by the SceneOpenGL to abstract away the
 * differences between various OpenGL windowing systems such as GLX and EGL.
 *
 * A concrete implementation has to create and release the OpenGL context in a way so that the
 * SceneOpenGL does not have to care about it.
 *
 * In addition a major task for this class is to generate the gl::texture_private which is
 * able to perform the texture from pixmap operation in the given backend.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
class KWIN_EXPORT backend
{
public:
    backend();
    virtual ~backend();

    /**
     * @return Time passes since start of rendering current frame.
     * @see startRenderTimer
     */
    qint64 renderTime()
    {
        return m_renderTimer.nsecsElapsed();
    }
    virtual void screenGeometryChanged(const QSize& size) = 0;
    virtual texture_private* createBackendTexture(gl::texture* texture) = 0;

    /**
     * @brief Backend specific code to prepare the rendering of a frame including flushing the
     * previously rendered frame to the screen if the backend works this way.
     *
     * @return A region that if not empty will be repainted in addition to the damaged region
     */
    virtual QRegion prepareRenderingFrame() = 0;

    /**
     * @brief Backend specific code to handle the end of rendering a frame.
     *
     * @param renderedRegion The possibly larger region that has been rendered
     * @param damagedRegion The damaged region that should be posted
     */
    virtual void endRenderingFrame(const QRegion& damage, const QRegion& damagedRegion) = 0;
    virtual void endRenderingFrameForScreen(base::output* output,
                                            const QRegion& damage,
                                            const QRegion& damagedRegion);
    virtual bool makeCurrent() = 0;
    virtual void doneCurrent() = 0;
    virtual bool hasSwapEvent() const
    {
        return true;
    }
    virtual QRegion prepareRenderingForScreen(base::output* output);
    /**
     * @brief Compositor is going into idle mode, flushes any pending paints.
     */
    void idle();

    /**
     * @return bool Whether the scene needs to flush a frame.
     */
    bool hasPendingFlush() const
    {
        return !m_lastDamage.isEmpty();
    }

    /**
     * @brief Whether the backend uses direct rendering.
     *
     * Some OpenGLScene modes require direct rendering. E.g. the OpenGL 2 should not be used
     * if direct rendering is not supported by the Scene.
     *
     * @return bool @c true if the GL context is direct, @c false if indirect
     */
    bool isDirectRendering() const
    {
        return m_directRendering;
    }

    bool supportsBufferAge() const
    {
        return m_haveBufferAge;
    }

    bool supportsSurfacelessContext() const
    {
        return m_haveSurfacelessContext;
    }

    /**
     * Returns the damage that has accumulated since a buffer of the given age was presented.
     */
    QRegion accumulatedDamageHistory(int bufferAge) const;

    /**
     * Saves the given region to damage history.
     */
    void addToDamageHistory(const QRegion& region);

    /**
     * The backend specific extensions (e.g. EGL/GLX extensions).
     *
     * Not the OpenGL (ES) extension!
     */
    QList<QByteArray> extensions() const
    {
        return m_extensions;
    }

    /**
     * @returns whether the backend specific extensions contains @p extension.
     */
    bool hasExtension(const QByteArray& extension) const
    {
        return m_extensions.contains(extension);
    }

    /**
     * Sets the platform-specific @p extensions.
     *
     * These are the EGL/GLX extensions, not the OpenGL extensions
     */
    void setExtensions(const QList<QByteArray>& extensions)
    {
        m_extensions = extensions;
    }

    /**
     * @brief Sets whether the OpenGL context is direct.
     *
     * Should be called by the concrete subclass once it is determined whether the OpenGL context is
     * direct or indirect.
     * If the subclass does not call this method, the backend defaults to @c false.
     *
     * @param direct @c true if the OpenGL context is direct, @c false if indirect
     */
    void setIsDirectRendering(bool direct)
    {
        m_directRendering = direct;
    }

    void setSupportsSurfacelessContext(bool value)
    {
        m_haveSurfacelessContext = value;
    }

    void setSupportsBufferAge(bool value)
    {
        m_haveBufferAge = value;
    }

    /**
     * Copy a region of pixels from the current read to the current draw buffer
     */
    void copyPixels(const QRegion& region);

    /**
     * For final backend-specific corrections to the scene projection matrix. Defaults to identity.
     */
    QMatrix4x4 transformation;

protected:
    /**
     * @brief Backend specific flushing of frame to screen.
     */
    virtual void present() = 0;

    /**
     * @return const QRegion& Damage of previously rendered frame
     */
    const QRegion& lastDamage() const
    {
        return m_lastDamage;
    }
    void setLastDamage(const QRegion& damage)
    {
        m_lastDamage = damage;
    }
    /**
     * @brief Starts the timer for how long it takes to render the frame.
     *
     * @see renderTime
     */
    void startRenderTimer()
    {
        m_renderTimer.start();
    }

private:
    /**
     * @brief Whether direct rendering is used, defaults to @c false.
     */
    bool m_directRendering;
    /**
     * @brief Whether the backend supports GLX_EXT_buffer_age / EGL_EXT_buffer_age.
     */
    bool m_haveBufferAge;
    /**
     * @brief Whether the backend supports EGL_KHR_surfaceless_context.
     */
    bool m_haveSurfacelessContext = false;
    /**
     * @brief Damaged region of previously rendered frame.
     */
    QRegion m_lastDamage;
    /**
     * @brief The damage history for the past 10 frames.
     */
    QList<QRegion> m_damageHistory;
    /**
     * @brief Timer to measure how long a frame renders.
     */
    QElapsedTimer m_renderTimer;

    QList<QByteArray> m_extensions;
};

}
}
