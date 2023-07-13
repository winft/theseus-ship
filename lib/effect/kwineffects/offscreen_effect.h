/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwingl/utils.h"
#include <kwineffects/effect.h>
#include <kwineffects/export.h>

namespace KWin
{

class OffscreenEffectPrivate;

/**
 * The OffscreenEffect class is the base class for effects that paint deformed windows.
 *
 * Under the hood, the OffscreenEffect will paint the window into an offscreen texture
 * and the offscreen texture will be transformed afterwards.
 *
 * The redirect() function must be called when the effect wants to transform a window.
 * Once the effect is no longer interested in the window, the unredirect() function
 * must be called.
 *
 * If a window is redirected into offscreen texture, the apply() function will be
 * called to transform the offscreen texture.
 */
class KWINEFFECTS_EXPORT OffscreenEffect : public Effect
{
    Q_OBJECT
public:
    explicit OffscreenEffect(QObject* parent = nullptr);
    ~OffscreenEffect() override;

    static bool supported();

    /**
     * If set our offscreen texture will be updated with the latest contents
     * It should be set before redirecting windows
     * The default is true
     */
    void setLive(bool live);

protected:
    void drawWindow(effect::window_paint_data& data) override;

    /**
     * This function must be called when the effect wants to animate the specified
     * @a window.
     */
    void redirect(EffectWindow* window);
    /**
     * This function must be called when the effect is done animating the specified
     * @a window. The window will be automatically unredirected if it's deleted.
     */
    void unredirect(EffectWindow const* window);

    /**
     * Override this function to transform the window.
     */
    virtual void apply(effect::window_paint_data& data, WindowQuadList& quads);

    /**
     * Allows to specify a @p shader to draw the redirected texture for @p window.
     * Can only be called once the window is redirected.
     * @since 5.25
     **/
    void setShader(EffectWindow const& window, GLShader* shader);

private Q_SLOTS:
    void handleWindowGeometryChanged(EffectWindow* window);
    void handleWindowDamaged(EffectWindow* window);
    void handleWindowDeleted(EffectWindow* window);

private:
    void setupConnections();
    void destroyConnections();

    QScopedPointer<OffscreenEffectPrivate> d;
};

}
