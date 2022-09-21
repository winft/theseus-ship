/*
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco_shadow.h"
#include "types.h"

#include "utils/algorithm.h"
#include "win/deco.h"
#include "win/window_qobject.h"

#include <kwineffects/window_quad.h>

#include <KDecoration2/DecorationShadow>
#include <QObject>
#include <QPixmap>
#include <functional>

namespace KWin::render
{

/**
 * @short Class representing a Window's Shadow to be rendered by the Compositor.
 *
 * This class holds all information about the Shadow to be rendered together with the
 * window during the Compositing stage. The Shadow consists of several pixmaps and offsets.
 * For a complete description please refer to https://community.kde.org/KWin/Shadow
 *
 * To create a Shadow instance use the static factory method createShadow which will
 * create an instance for the currently used Compositing Backend. It will read the X11 Property
 * and create the Shadow and all required data (such as WindowQuads). If there is no Shadow
 * defined for the window the factory method returns @c NULL.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @todo React on window size changes.
 */
template<typename Window>
class shadow : public QObject
{
public:
    /**
     * @return Region of the shadow.
     */
    const QRegion& shadowRegion() const
    {
        return m_shadowRegion;
    }

    /**
     * @return Cached Shadow Quads
     */
    const WindowQuadList& shadowQuads() const
    {
        return m_shadowQuads;
    }

    WindowQuadList& shadowQuads()
    {
        return m_shadowQuads;
    }

    /**
     * This method updates the Shadow when the property has been changed.
     * It is the responsibility of the owner of the Shadow to call this method
     * whenever the owner receives a PropertyNotify event.
     * This method will invoke a re-read of the Property. In case the Property has
     * been withdrawn the method returns @c false. In that case the owner should
     * delete the Shadow.
     * @returns @c true when the shadow has been updated, @c false if the property is not set
     * anymore.
     */
    bool updateShadow()
    {
        assert(window);

        if (m_decorationShadow) {
            if (window->ref_win->control) {
                if (auto deco = win::decoration(window->ref_win)) {
                    if (update_deco_shadow(*this, deco)) {
                        return true;
                    }
                }
            }
            return false;
        }

        if (auto& win_update = window->shadow_windowing.update; win_update) {
            return win_update(*this);
        }

        return false;
    }

    bool hasDecorationShadow() const
    {
        return !m_decorationShadow.isNull();
    }

    QImage decorationShadowImage() const
    {
        if (!m_decorationShadow) {
            return QImage();
        }
        return m_decorationShadow->shadow();
    }

    QWeakPointer<KDecoration2::DecorationShadow> decorationShadow() const
    {
        return m_decorationShadow.toWeakRef();
    }

    QMargins margins() const
    {
        return QMargins(m_leftOffset, m_topOffset, m_rightOffset, m_topOffset);
    }

    void updateShadowRegion()
    {
        auto const size = window->ref_win->geo.size();
        const QRect top(0, -m_topOffset, size.width(), m_topOffset);
        const QRect right(size.width(),
                          -m_topOffset,
                          m_rightOffset,
                          size.height() + m_topOffset + m_bottomOffset);
        const QRect bottom(0, size.height(), size.width(), m_bottomOffset);
        const QRect left(-m_leftOffset,
                         -m_topOffset,
                         m_leftOffset,
                         size.height() + m_topOffset + m_bottomOffset);
        m_shadowRegion = QRegion(top).united(right).united(bottom).united(left);
    }

    virtual bool prepareBackend() = 0;
    virtual void buildQuads()
    {
        // prepare window quads
        m_shadowQuads.clear();

        auto const size = window->ref_win->geo.size();
        const QSize top(m_shadowElements[enum_index(shadow_element::top)].size());
        const QSize topRight(m_shadowElements[enum_index(shadow_element::top_right)].size());
        const QSize right(m_shadowElements[enum_index(shadow_element::right)].size());
        const QSize bottomRight(m_shadowElements[enum_index(shadow_element::bottom_right)].size());
        const QSize bottom(m_shadowElements[enum_index(shadow_element::bottom)].size());
        const QSize bottomLeft(m_shadowElements[enum_index(shadow_element::bottom_left)].size());
        const QSize left(m_shadowElements[enum_index(shadow_element::left)].size());
        const QSize topLeft(m_shadowElements[enum_index(shadow_element::top_left)].size());
        if ((left.width() - m_leftOffset > size.width())
            || (right.width() - m_rightOffset > size.width())
            || (top.height() - m_topOffset > size.height())
            || (bottom.height() - m_bottomOffset > size.height())) {
            // if our shadow is bigger than the window, we don't render the shadow
            m_shadowRegion = QRegion();
            return;
        }

        const QRect outerRect(QPoint(-m_leftOffset, -m_topOffset),
                              QPoint(size.width() + m_rightOffset, size.height() + m_bottomOffset));

        WindowQuad topLeftQuad(WindowQuadShadowTopLeft);
        topLeftQuad[0] = WindowVertex(outerRect.x(), outerRect.y(), 0.0, 0.0);
        topLeftQuad[1] = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y(), 1.0, 0.0);
        topLeftQuad[2] = WindowVertex(
            outerRect.x() + topLeft.width(), outerRect.y() + topLeft.height(), 1.0, 1.0);
        topLeftQuad[3] = WindowVertex(outerRect.x(), outerRect.y() + topLeft.height(), 0.0, 1.0);
        m_shadowQuads.append(topLeftQuad);

        WindowQuad topQuad(WindowQuadShadowTop);
        topQuad[0] = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y(), 0.0, 0.0);
        topQuad[1] = WindowVertex(outerRect.right() - topRight.width(), outerRect.y(), 1.0, 0.0);
        topQuad[2] = WindowVertex(
            outerRect.right() - topRight.width(), outerRect.y() + top.height(), 1.0, 1.0);
        topQuad[3]
            = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y() + top.height(), 0.0, 1.0);
        m_shadowQuads.append(topQuad);

        WindowQuad topRightQuad(WindowQuadShadowTopRight);
        topRightQuad[0]
            = WindowVertex(outerRect.right() - topRight.width(), outerRect.y(), 0.0, 0.0);
        topRightQuad[1] = WindowVertex(outerRect.right(), outerRect.y(), 1.0, 0.0);
        topRightQuad[2]
            = WindowVertex(outerRect.right(), outerRect.y() + topRight.height(), 1.0, 1.0);
        topRightQuad[3] = WindowVertex(
            outerRect.right() - topRight.width(), outerRect.y() + topRight.height(), 0.0, 1.0);
        m_shadowQuads.append(topRightQuad);

        WindowQuad rightQuad(WindowQuadShadowRight);
        rightQuad[0] = WindowVertex(
            outerRect.right() - right.width(), outerRect.y() + topRight.height(), 0.0, 0.0);
        rightQuad[1] = WindowVertex(outerRect.right(), outerRect.y() + topRight.height(), 1.0, 0.0);
        rightQuad[2]
            = WindowVertex(outerRect.right(), outerRect.bottom() - bottomRight.height(), 1.0, 1.0);
        rightQuad[3] = WindowVertex(
            outerRect.right() - right.width(), outerRect.bottom() - bottomRight.height(), 0.0, 1.0);
        m_shadowQuads.append(rightQuad);

        WindowQuad bottomRightQuad(WindowQuadShadowBottomRight);
        bottomRightQuad[0] = WindowVertex(outerRect.right() - bottomRight.width(),
                                          outerRect.bottom() - bottomRight.height(),
                                          0.0,
                                          0.0);
        bottomRightQuad[1]
            = WindowVertex(outerRect.right(), outerRect.bottom() - bottomRight.height(), 1.0, 0.0);
        bottomRightQuad[2] = WindowVertex(outerRect.right(), outerRect.bottom(), 1.0, 1.0);
        bottomRightQuad[3]
            = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom(), 0.0, 1.0);
        m_shadowQuads.append(bottomRightQuad);

        WindowQuad bottomQuad(WindowQuadShadowBottom);
        bottomQuad[0] = WindowVertex(
            outerRect.x() + bottomLeft.width(), outerRect.bottom() - bottom.height(), 0.0, 0.0);
        bottomQuad[1] = WindowVertex(outerRect.right() - bottomRight.width(),
                                     outerRect.bottom() - bottom.height(),
                                     1.0,
                                     0.0);
        bottomQuad[2]
            = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom(), 1.0, 1.0);
        bottomQuad[3]
            = WindowVertex(outerRect.x() + bottomLeft.width(), outerRect.bottom(), 0.0, 1.0);
        m_shadowQuads.append(bottomQuad);

        WindowQuad bottomLeftQuad(WindowQuadShadowBottomLeft);
        bottomLeftQuad[0]
            = WindowVertex(outerRect.x(), outerRect.bottom() - bottomLeft.height(), 0.0, 0.0);
        bottomLeftQuad[1] = WindowVertex(
            outerRect.x() + bottomLeft.width(), outerRect.bottom() - bottomLeft.height(), 1.0, 0.0);
        bottomLeftQuad[2]
            = WindowVertex(outerRect.x() + bottomLeft.width(), outerRect.bottom(), 1.0, 1.0);
        bottomLeftQuad[3] = WindowVertex(outerRect.x(), outerRect.bottom(), 0.0, 1.0);
        m_shadowQuads.append(bottomLeftQuad);

        WindowQuad leftQuad(WindowQuadShadowLeft);
        leftQuad[0] = WindowVertex(outerRect.x(), outerRect.y() + topLeft.height(), 0.0, 0.0);
        leftQuad[1] = WindowVertex(
            outerRect.x() + left.width(), outerRect.y() + topLeft.height(), 1.0, 0.0);
        leftQuad[2] = WindowVertex(
            outerRect.x() + left.width(), outerRect.bottom() - bottomLeft.height(), 1.0, 1.0);
        leftQuad[3]
            = WindowVertex(outerRect.x(), outerRect.bottom() - bottomLeft.height(), 0.0, 1.0);
        m_shadowQuads.append(leftQuad);
    }

    void geometryChanged()
    {
        if (m_cachedSize == window->ref_win->geo.size()) {
            return;
        }
        m_cachedSize = window->ref_win->geo.size();
        updateShadowRegion();
        buildQuads();
    }

    // shadow pixmaps
    QPixmap m_shadowElements[static_cast<size_t>(shadow_element::count)];

    // shadow offsets
    int m_topOffset;
    int m_rightOffset;
    int m_bottomOffset;
    int m_leftOffset;

    // Decoration based shadows
    QSharedPointer<KDecoration2::DecorationShadow> m_decorationShadow;

    Window* window;

protected:
    shadow(Window* window)
        : window{window}
        , m_cachedSize(window->ref_win->geo.size())
    {
        QObject::connect(window->ref_win->qobject.get(),
                         &win::window_qobject::frame_geometry_changed,
                         this,
                         &shadow::geometryChanged);
    }

    inline const QPixmap& shadowPixmap(shadow_element element) const
    {
        return m_shadowElements[static_cast<size_t>(element)];
    }

    QSize elementSize(shadow_element element) const
    {
        if (m_decorationShadow) {
            switch (element) {
            case shadow_element::top:
                return m_decorationShadow->topGeometry().size();
            case shadow_element::top_right:
                return m_decorationShadow->topRightGeometry().size();
            case shadow_element::right:
                return m_decorationShadow->rightGeometry().size();
            case shadow_element::bottom_right:
                return m_decorationShadow->bottomRightGeometry().size();
            case shadow_element::bottom:
                return m_decorationShadow->bottomGeometry().size();
            case shadow_element::bottom_left:
                return m_decorationShadow->bottomLeftGeometry().size();
            case shadow_element::left:
                return m_decorationShadow->leftGeometry().size();
            case shadow_element::top_left:
                return m_decorationShadow->topLeftGeometry().size();
            default:
                return QSize();
            }
        } else {
            return m_shadowElements[enum_index(element)].size();
        }
    }

    int topOffset() const
    {
        return m_topOffset;
    }

    int rightOffset() const
    {
        return m_rightOffset;
    }

    int bottomOffset() const
    {
        return m_bottomOffset;
    }

    int leftOffset() const
    {
        return m_leftOffset;
    }

    void setShadowRegion(const QRegion& region)
    {
        m_shadowRegion = region;
    }

    void setShadowElement(const QPixmap& shadow, shadow_element element)
    {
        m_shadowElements[enum_index(element)] = shadow;
    }

    WindowQuadList m_shadowQuads;

private:
    // caches
    QRegion m_shadowRegion;
    QSize m_cachedSize;
};

template<typename Window>
struct shadow_windowing_integration {
    std::function<std::unique_ptr<shadow<Window>>(Window&)> create;
    std::function<bool(shadow<Window>&)> update;
};

}
