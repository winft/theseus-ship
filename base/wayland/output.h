/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019, 2021 Roman Gilg <subdiff@gmail.com>

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

#include "output_transform.h"

#include "base/output.h"
#include "kwin_export.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <Wrapland/Server/output.h>
#include <memory>

namespace Wrapland
{
namespace Server
{
class OutputChangesetV1;
}
}

namespace KWin
{

namespace render::wayland
{
class output;
}

namespace base::wayland
{

/**
 * Generic output representation in a Wayland session
 */
class KWIN_EXPORT output : public base::output
{
    Q_OBJECT
public:
    ~output();

    QString name() const override;

    /**
     * The mode size is the current hardware mode of the output in pixel
     * and is dependent on hardware parameters but can often be adjusted. In most
     * cases running the maximum resolution is preferred though since this has the
     * best picture quality.
     *
     * @return output mode size
     */
    QSize mode_size() const;

    // TODO: The name is ambiguous. Rename this function.
    QSize pixel_size() const override;

    /**
     * Describes the viewable rectangle on the output relative to the output's mode size.
     *
     * Per default the view spans the full output.
     *
     * @return viewable geometry on the output
     */
    QRect view_geometry() const;

    qreal scale() const override;

    /**
     * The geometry of this output in global compositor co-ordinates (i.e scaled)
     */
    QRect geometry() const override;
    QSize physical_size() const override;

    /**
     * Returns the orientation of this output.
     *
     * - Flipped along the vertical axis is landscape + inv. portrait.
     * - Rotated 90° and flipped along the horizontal axis is portrait + inv. landscape
     * - Rotated 180° and flipped along the vertical axis is inv. landscape + inv. portrait
     * - Rotated 270° and flipped along the horizontal axis is inv. portrait + inv. landscape +
     *   portrait
     */
    base::wayland::output_transform transform() const;

    /**
     * Current refresh rate in 1/ms.
     */
    int refresh_rate() const override;

    bool is_internal() const override
    {
        return m_internal;
    }

    void apply_changes(const Wrapland::Server::OutputChangesetV1* changeset);

    Wrapland::Server::Output* wrapland_output() const
    {
        return m_output.get();
    }

    bool is_enabled() const;
    /**
     * Enable or disable the output.
     *
     * This differs from update_dpms as it also removes the wl_output.
     * The default is on.
     */
    void set_enabled(bool enable) override;

    void force_geometry(QRectF const& geo);

    bool is_dpms_on() const override;
    virtual uint64_t msc() const;

    QSize orientate_size(QSize const& size) const;

    std::unique_ptr<render::wayland::output> render;
    std::unique_ptr<Wrapland::Server::Output> m_output;
    base::dpms_mode m_dpms{base::dpms_mode::on};

Q_SIGNALS:
    void mode_changed();

protected:
    void init_interfaces(std::string const& name,
                         std::string const& make,
                         std::string const& model,
                         std::string const& serial_number,
                         QSize const& physical_size,
                         QVector<Wrapland::Server::Output::Mode> const& modes,
                         Wrapland::Server::Output::Mode* current_mode = nullptr);

    QPoint global_pos() const;

    bool internal() const
    {
        return m_internal;
    }
    void set_internal(bool set)
    {
        m_internal = set;
    }
    void set_dpms_supported(bool set)
    {
        m_supports_dpms = set;
    }

    virtual void update_enablement(bool enable)
    {
        Q_UNUSED(enable);
    }
    virtual void update_mode(int mode_index)
    {
        Q_UNUSED(mode_index);
    }
    virtual void update_transform(base::wayland::output_transform transform)
    {
        Q_UNUSED(transform);
    }

    void set_wayland_mode(QSize const& size, int refresh_rate, bool force_update);
    void set_transform(base::wayland::output_transform transform);

    base::dpms_mode dpms_mode() const;

private:
    QSizeF logical_size() const;
    void update_view_geometry();

    QRect m_view_geometry;

    bool m_internal = false;
    bool m_supports_dpms = false;
};

}
}
