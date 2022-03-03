/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect.h>
#include <kwineffects/window_quad.h>
#include <kwineffects_export.h>

namespace KWin
{

class EffectScreen;
class GLShader;
class PaintDataPrivate;
class WindowPaintDataPrivate;

class KWINEFFECTS_EXPORT PaintData
{
public:
    virtual ~PaintData();
    /**
     * @returns scale factor in X direction.
     * @since 4.10
     */
    qreal xScale() const;
    /**
     * @returns scale factor in Y direction.
     * @since 4.10
     */
    qreal yScale() const;
    /**
     * @returns scale factor in Z direction.
     * @since 4.10
     */
    qreal zScale() const;
    /**
     * Sets the scale factor in X direction to @p scale
     * @param scale The scale factor in X direction
     * @since 4.10
     */
    void setXScale(qreal scale);
    /**
     * Sets the scale factor in Y direction to @p scale
     * @param scale The scale factor in Y direction
     * @since 4.10
     */
    void setYScale(qreal scale);
    /**
     * Sets the scale factor in Z direction to @p scale
     * @param scale The scale factor in Z direction
     * @since 4.10
     */
    void setZScale(qreal scale);
    /**
     * Sets the scale factor in X and Y direction.
     * @param scale The scale factor for X and Y direction
     * @since 4.10
     */
    void setScale(const QVector2D& scale);
    /**
     * Sets the scale factor in X, Y and Z direction
     * @param scale The scale factor for X, Y and Z direction
     * @since 4.10
     */
    void setScale(const QVector3D& scale);
    const QVector3D& scale() const;
    const QVector3D& translation() const;
    /**
     * @returns the translation in X direction.
     * @since 4.10
     */
    qreal xTranslation() const;
    /**
     * @returns the translation in Y direction.
     * @since 4.10
     */
    qreal yTranslation() const;
    /**
     * @returns the translation in Z direction.
     * @since 4.10
     */
    qreal zTranslation() const;
    /**
     * Sets the translation in X direction to @p translate.
     * @since 4.10
     */
    void setXTranslation(qreal translate);
    /**
     * Sets the translation in Y direction to @p translate.
     * @since 4.10
     */
    void setYTranslation(qreal translate);
    /**
     * Sets the translation in Z direction to @p translate.
     * @since 4.10
     */
    void setZTranslation(qreal translate);
    /**
     * Performs a translation by adding the values component wise.
     * @param x Translation in X direction
     * @param y Translation in Y direction
     * @param z Translation in Z direction
     * @since 4.10
     */
    void translate(qreal x, qreal y = 0.0, qreal z = 0.0);
    /**
     * Performs a translation by adding the values component wise.
     * Overloaded method for convenience.
     * @param translate The translation
     * @since 4.10
     */
    void translate(const QVector3D& translate);

    /**
     * Sets the rotation angle.
     * @param angle The new rotation angle.
     * @since 4.10
     * @see rotationAngle()
     */
    void setRotationAngle(qreal angle);
    /**
     * Returns the rotation angle.
     * Initially 0.0.
     * @returns The current rotation angle.
     * @since 4.10
     * @see setRotationAngle
     */
    qreal rotationAngle() const;
    /**
     * Sets the rotation origin.
     * @param origin The new rotation origin.
     * @since 4.10
     * @see rotationOrigin()
     */
    void setRotationOrigin(const QVector3D& origin);
    /**
     * Returns the rotation origin. That is the point in space which is fixed during the rotation.
     * Initially this is 0/0/0.
     * @returns The rotation's origin
     * @since 4.10
     * @see setRotationOrigin()
     */
    QVector3D rotationOrigin() const;
    /**
     * Sets the rotation axis.
     * Set a component to 1.0 to rotate around this axis and to 0.0 to disable rotation around the
     * axis.
     * @param axis A vector holding information on which axis to rotate
     * @since 4.10
     * @see rotationAxis()
     */
    void setRotationAxis(const QVector3D& axis);
    /**
     * Sets the rotation axis.
     * Overloaded method for convenience.
     * @param axis The axis around which should be rotated.
     * @since 4.10
     * @see rotationAxis()
     */
    void setRotationAxis(Qt::Axis axis);
    /**
     * The current rotation axis.
     * By default the rotation is (0/0/1) which means a rotation around the z axis.
     * @returns The current rotation axis.
     * @since 4.10
     * @see setRotationAxis
     */
    QVector3D rotationAxis() const;

protected:
    PaintData();
    PaintData(const PaintData& other);

private:
    PaintDataPrivate* const d;
};

class KWINEFFECTS_EXPORT WindowPrePaintData
{
public:
    int mask;
    /**
     * Region that will be painted, in screen coordinates.
     */
    QRegion paint;
    /**
     * The clip region will be subtracted from paint region of following windows.
     * I.e. window will definitely cover it's clip region
     */
    QRegion clip;
    WindowQuadList quads;
    /**
     * Simple helper that sets data to say the window will be painted as non-opaque.
     * Takes also care of changing the regions.
     */
    void setTranslucent()
    {
        mask |= Effect::PAINT_WINDOW_TRANSLUCENT;
        mask &= ~Effect::PAINT_WINDOW_OPAQUE;
        clip = QRegion(); // cannot clip, will be transparent
    }

    /**
     * Helper to mark that this window will be transformed
     */
    void setTransformed()
    {
        mask |= Effect::PAINT_WINDOW_TRANSFORMED;
    }
};

class KWINEFFECTS_EXPORT WindowPaintData : public PaintData
{
public:
    explicit WindowPaintData(EffectWindow* w);
    explicit WindowPaintData(EffectWindow* w, const QMatrix4x4& screenProjectionMatrix);
    WindowPaintData(const WindowPaintData& other);
    ~WindowPaintData() override;
    /**
     * Scales the window by @p scale factor.
     * Multiplies all three components by the given factor.
     * @since 4.10
     */
    WindowPaintData& operator*=(qreal scale);
    /**
     * Scales the window by @p scale factor.
     * Performs a component wise multiplication on x and y components.
     * @since 4.10
     */
    WindowPaintData& operator*=(const QVector2D& scale);
    /**
     * Scales the window by @p scale factor.
     * Performs a component wise multiplication.
     * @since 4.10
     */
    WindowPaintData& operator*=(const QVector3D& scale);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QPointF& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QPoint& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QVector2D& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QVector3D& translation);
    /**
     * Window opacity, in range 0 = transparent to 1 = fully opaque
     * @see setOpacity
     * @since 4.10
     */
    qreal opacity() const;
    /**
     * Sets the window opacity to the new @p opacity.
     * If you want to modify the existing opacity level consider using multiplyOpacity.
     * @param opacity The new opacity level
     * @since 4.10
     */
    void setOpacity(qreal opacity);
    /**
     * Multiplies the current opacity with the @p factor.
     * @param factor Factor with which the opacity should be multiplied
     * @return New opacity level
     * @since 4.10
     */
    qreal multiplyOpacity(qreal factor);
    /**
     * Saturation of the window, in range [0; 1]
     * 1 means that the window is unchanged, 0 means that it's completely
     *  unsaturated (greyscale). 0.5 would make the colors less intense,
     *  but not completely grey
     * Use EffectsHandler::saturationSupported() to find out whether saturation
     * is supported by the system, otherwise this value has no effect.
     * @return The current saturation
     * @see setSaturation()
     * @since 4.10
     */
    qreal saturation() const;
    /**
     * Sets the window saturation level to @p saturation.
     * If you want to modify the existing saturation level consider using multiplySaturation.
     * @param saturation The new saturation level
     * @since 4.10
     */
    void setSaturation(qreal saturation) const;
    /**
     * Multiplies the current saturation with @p factor.
     * @param factor with which the saturation should be multiplied
     * @return New saturation level
     * @since 4.10
     */
    qreal multiplySaturation(qreal factor);
    /**
     * Brightness of the window, in range [0; 1]
     * 1 means that the window is unchanged, 0 means that it's completely
     * black. 0.5 would make it 50% darker than usual
     */
    qreal brightness() const;
    /**
     * Sets the window brightness level to @p brightness.
     * If you want to modify the existing brightness level consider using multiplyBrightness.
     * @param brightness The new brightness level
     */
    void setBrightness(qreal brightness);
    /**
     * Multiplies the current brightness level with @p factor.
     * @param factor with which the brightness should be multiplied.
     * @return New brightness level
     * @since 4.10
     */
    qreal multiplyBrightness(qreal factor);
    /**
     * The screen number for which the painting should be done.
     * This affects color correction (different screens may need different
     * color correction lookup tables because they have different ICC profiles).
     * @return screen for which painting should be done
     */
    int screen() const;
    /**
     * @param screen New screen number
     * A value less than 0 will indicate that a default profile should be done.
     */
    void setScreen(int screen) const;
    /**
     * @brief Sets the cross fading @p factor to fade over with previously sized window.
     * If @c 1.0 only the current window is used, if @c 0.0 only the previous window is used.
     *
     * By default only the current window is used. This factor can only make any visual difference
     * if the previous window get referenced.
     *
     * @param factor The cross fade factor between @c 0.0 (previous window) and @c 1.0 (current
     * window)
     * @see crossFadeProgress
     */
    void setCrossFadeProgress(qreal factor);
    /**
     * @see setCrossFadeProgress
     */
    qreal crossFadeProgress() const;

    /**
     * Sets the projection matrix that will be used when painting the window.
     *
     * The default projection matrix can be overridden by setting this matrix
     * to a non-identity matrix.
     */
    void setProjectionMatrix(const QMatrix4x4& matrix);

    /**
     * Returns the current projection matrix.
     *
     * The default value for this matrix is the identity matrix.
     */
    QMatrix4x4 projectionMatrix() const;

    /**
     * Returns a reference to the projection matrix.
     */
    QMatrix4x4& rprojectionMatrix();

    /**
     * Sets the model-view matrix that will be used when painting the window.
     *
     * The default model-view matrix can be overridden by setting this matrix
     * to a non-identity matrix.
     */
    void setModelViewMatrix(const QMatrix4x4& matrix);

    /**
     * Returns the current model-view matrix.
     *
     * The default value for this matrix is the identity matrix.
     */
    QMatrix4x4 modelViewMatrix() const;

    /**
     * Returns a reference to the model-view matrix.
     */
    QMatrix4x4& rmodelViewMatrix();

    /**
     * Returns The projection matrix as used by the current screen painting pass
     * including screen transformations.
     *
     * @since 5.6
     */
    QMatrix4x4 screenProjectionMatrix() const;

    WindowQuadList quads;

    /**
     * Shader to be used for rendering, if any.
     */
    GLShader* shader;

private:
    WindowPaintDataPrivate* const d;
};

class ScreenPrePaintData
{
public:
    int mask;
    QRegion paint;
};

class KWINEFFECTS_EXPORT ScreenPaintData : public PaintData
{
public:
    ScreenPaintData();
    ScreenPaintData(const QMatrix4x4& projectionMatrix, EffectScreen* screen = nullptr);
    ScreenPaintData(const ScreenPaintData& other);
    ~ScreenPaintData() override;
    /**
     * Scales the screen by @p scale factor.
     * Multiplies all three components by the given factor.
     * @since 4.10
     */
    ScreenPaintData& operator*=(qreal scale);
    /**
     * Scales the screen by @p scale factor.
     * Performs a component wise multiplication on x and y components.
     * @since 4.10
     */
    ScreenPaintData& operator*=(const QVector2D& scale);
    /**
     * Scales the screen by @p scale factor.
     * Performs a component wise multiplication.
     * @since 4.10
     */
    ScreenPaintData& operator*=(const QVector3D& scale);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QPointF& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QPoint& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QVector2D& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QVector3D& translation);
    ScreenPaintData& operator=(const ScreenPaintData& rhs);

    /**
     * The projection matrix used by the scene for the current rendering pass.
     * On non-OpenGL compositors it's set to Identity matrix.
     * @since 5.6
     */
    QMatrix4x4 projectionMatrix() const;

    /**
     * Returns the currently rendered screen. Only set for per-screen rendering, e.g. Wayland.
     */
    EffectScreen* screen() const;

private:
    class Private;
    QScopedPointer<Private> d;
};

}
