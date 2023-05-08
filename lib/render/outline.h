/*
SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "base/config.h"
#include "kwin_export.h"
#include "kwinglobals.h"

#include <QObject>
#include <QRect>
#include <memory>

class QQmlContext;
class QQmlComponent;
class QQmlEngine;

namespace KWin::render
{

class outline_visual;
using outline_visual_factory = std::function<std::unique_ptr<outline_visual>()>;

/**
 * @short This class is used to show the outline of a given geometry.
 *
 * The class renders an outline by using four windows. One for each border of
 * the geometry. It is possible to replace the outline with an effect. If an
 * effect is available the effect will be used, otherwise the outline will be
 * rendered by using the X implementation.
 *
 * @author Arthur Arlt
 * @since 4.7
 */
class KWIN_EXPORT outline : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
    Q_PROPERTY(
        QRect visualParentGeometry READ visualParentGeometry NOTIFY visualParentGeometryChanged)
    Q_PROPERTY(QRect unifiedGeometry READ unifiedGeometry NOTIFY unifiedGeometryChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
public:
    template<typename Compositor>
    static std::unique_ptr<outline> create(Compositor& compositor,
                                           outline_visual_factory visual_factory)

    {
        auto outline = std::make_unique<render::outline>(visual_factory);
        connect(compositor.qobject.get(),
                &decltype(compositor.qobject)::element_type::compositingToggled,
                outline.get(),
                &outline::compositingChanged);
        return outline;
    }

    outline(outline_visual_factory visual_factory);
    ~outline() override;

    /**
     * Set the outline geometry.
     * To show the outline use showOutline.
     * @param outlineGeometry The geometry of the outline to be shown
     * @see showOutline
     */
    void setGeometry(const QRect& outlineGeometry);

    /**
     * Set the visual parent geometry.
     * This is the geometry from which the will emerge.
     * @param visualParentGeometry The visual geometry of the visual parent
     * @see showOutline
     */
    void setVisualParentGeometry(const QRect& visualParentGeometry);

    /**
     * Shows the outline of a window using either an effect or the X implementation.
     * To stop the outline process use hideOutline.
     * @see hideOutline
     */
    void show();

    /**
     * Shows the outline for the given @p outlineGeometry.
     * This is the same as setOutlineGeometry followed by showOutline directly.
     * To stop the outline process use hideOutline.
     * @param outlineGeometry The geometry of the outline to be shown
     * @see hideOutline
     */
    void show(const QRect& outlineGeometry);

    /**
     * Shows the outline for the given @p outlineGeometry animated from @p visualParentGeometry.
     * This is the same as setOutlineGeometry followed by setVisualParentGeometry
     * and then showOutline.
     * To stop the outline process use hideOutline.
     * @param outlineGeometry The geometry of the outline to be shown
     * @param visualParentGeometry The geometry from where the outline should emerge
     * @see hideOutline
     * @since 5.10
     */
    void show(const QRect& outlineGeometry, const QRect& visualParentGeometry);

    /**
     * Hides shown outline.
     * @see showOutline
     */
    void hide();

    const QRect& geometry() const;
    const QRect& visualParentGeometry() const;
    QRect unifiedGeometry() const;

    bool isActive() const;

private Q_SLOTS:
    void compositingChanged();

Q_SIGNALS:
    void activeChanged();
    void geometryChanged();
    void unifiedGeometryChanged();
    void visualParentGeometryChanged();

private:
    std::unique_ptr<outline_visual> m_visual;
    QRect m_outlineGeometry;
    QRect m_visualParentGeometry;
    bool m_active{false};
    outline_visual_factory visual_factory;
};

class KWIN_EXPORT outline_visual
{
public:
    outline_visual(render::outline* outline);
    virtual ~outline_visual();
    virtual void show() = 0;
    virtual void hide() = 0;

protected:
    outline* get_outline();
    outline const* get_outline() const;

private:
    render::outline* m_outline;
};

class KWIN_EXPORT composited_outline_visual : public outline_visual
{
public:
    composited_outline_visual(render::outline* outline, QQmlEngine& engine, base::config& config);
    ~composited_outline_visual() override;
    void show() override;
    void hide() override;

private:
    QScopedPointer<QQmlContext> m_qmlContext;
    QScopedPointer<QQmlComponent> m_qmlComponent;
    QScopedPointer<QObject> m_mainItem;
    QQmlEngine& engine;
    base::config& config;
};

inline bool outline::isActive() const
{
    return m_active;
}

inline const QRect& outline::geometry() const
{
    return m_outlineGeometry;
}

inline const QRect& outline::visualParentGeometry() const
{
    return m_visualParentGeometry;
}

inline outline* outline_visual::get_outline()
{
    return m_outline;
}

inline const outline* outline_visual::get_outline() const
{
    return m_outline;
}

template<typename Compositor, typename Outline>
std::unique_ptr<outline_visual> create_outline_visual(Compositor& compositor, Outline& outline)
{
    if (compositor.state == state::on) {
        return std::make_unique<composited_outline_visual>(
            &outline,
            *compositor.platform.base.script->qml_engine,
            compositor.platform.base.config);
    } else {
        return std::unique_ptr<outline_visual>(
            compositor.platform.create_non_composited_outline(&outline));
    }
}

}
