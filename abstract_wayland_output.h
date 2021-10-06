/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#ifndef KWIN_ABSTRACT_WAYLAND_OUTPUT_H
#define KWIN_ABSTRACT_WAYLAND_OUTPUT_H

#include "abstract_output.h"
#include <utils.h>
#include <kwin_export.h>

#include <QObject>
#include <QPoint>
#include <memory>
#include <QRect>
#include <QSize>
#include <QVector>

#include <Wrapland/Server/output.h>

namespace Wrapland
{
namespace Server
{
class OutputChangesetV1;
}
}

namespace KWin
{

/**
 * Generic output representation in a Wayland session
 */
class KWIN_EXPORT AbstractWaylandOutput : public AbstractOutput
{
    Q_OBJECT
public:
    enum class Transform {
        Normal,
        Rotated90,
        Rotated180,
        Rotated270,
        Flipped,
        Flipped90,
        Flipped180,
        Flipped270
    };

    explicit AbstractWaylandOutput(QObject *parent = nullptr);

    QString name() const override;

    /**
     * The mode size is the current hardware mode of the output in pixel
     * and is dependent on hardware parameters but can often be adjusted. In most
     * cases running the maximum resolution is preferred though since this has the
     * best picture quality.
     *
     * @return output mode size
     */
    QSize modeSize() const;

    // TODO: The name is ambiguous. Rename this function.
    QSize pixelSize() const override;

    /**
     * Describes the viewable rectangle on the output relative to the output's mode size.
     *
     * Per default the view spans the full output.
     *
     * @return viewable geometry on the output
     */
    QRect viewGeometry() const;

    qreal scale() const override;

    /**
     * The geometry of this output in global compositor co-ordinates (i.e scaled)
     */
    QRect geometry() const override;
    QSize physicalSize() const override;

    /**
     * Returns the orientation of this output.
     *
     * - Flipped along the vertical axis is landscape + inv. portrait.
     * - Rotated 90° and flipped along the horizontal axis is portrait + inv. landscape
     * - Rotated 180° and flipped along the vertical axis is inv. landscape + inv. portrait
     * - Rotated 270° and flipped along the horizontal axis is inv. portrait + inv. landscape +
     *   portrait
     */
    Transform transform() const;

    /**
     * Current refresh rate in 1/ms.
     */
    int refreshRate() const override;

    bool isInternal() const override {
        return m_internal;
    }

    void applyChanges(const Wrapland::Server::OutputChangesetV1 *changeset) override;

    Wrapland::Server::Output* output() const {
        return m_output.get();
    }

    bool isEnabled() const;
    /**
     * Enable or disable the output.
     *
     * This differs from updateDpms as it also removes the wl_output.
     * The default is on.
     */
    void setEnabled(bool enable) override;

    void forceGeometry(const QRectF &geo);

    bool dpmsOn() const override;
    virtual uint64_t msc() const;

    QSize orientateSize(const QSize &size) const;

Q_SIGNALS:
    void modeChanged();

protected:
    void initInterfaces(std::string const& name, std::string const& make,
                        std::string const& model, std::string const& serial_number,
                        const QSize &physicalSize,
                        const QVector<Wrapland::Server::Output::Mode> &modes,
                        Wrapland::Server::Output::Mode *current_mode = nullptr);

    QPoint globalPos() const;

    bool internal() const {
        return m_internal;
    }
    void setInternal(bool set) {
        m_internal = set;
    }
    void setDpmsSupported(bool set) {
        m_supportsDpms = set;
    }

    virtual void updateEnablement(bool enable) {
        Q_UNUSED(enable);
    }
    virtual void updateMode(int modeIndex) {
        Q_UNUSED(modeIndex);
    }
    virtual void updateTransform(Transform transform) {
        Q_UNUSED(transform);
    }

    void setWaylandMode(const QSize &size, int refreshRate, bool force_update);
    void setTransform(Transform transform);

    DpmsMode dpmsMode() const;
    void dpmsSetOn();
    void dpmsSetOff(DpmsMode mode);

private:
    QSizeF logicalSize() const;
    void updateViewGeometry();

    std::unique_ptr<Wrapland::Server::Output> m_output;

    DpmsMode m_dpms = DpmsMode::On;
    QRect m_viewGeometry;

    bool m_internal = false;
    bool m_supportsDpms = false;
};

}

#endif // KWIN_OUTPUT_H
