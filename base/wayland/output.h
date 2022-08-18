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

#include "output_helpers.h"
#include "output_transform.h"

#include "base/logging.h"
#include "base/output.h"
#include "render/wayland/output.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <Wrapland/Server/output.h>
#include <Wrapland/Server/output_changeset_v1.h>
#include <memory>

namespace KWin::base::wayland
{

template<typename Platform>
class output : public base::output
{
public:
    QString name() const override
    {
        return QString::fromStdString(m_output->name());
    }

    /**
     * The mode size is the current hardware mode of the output in pixel
     * and is dependent on hardware parameters but can often be adjusted. In most
     * cases running the maximum resolution is preferred though since this has the
     * best picture quality.
     *
     * @return output mode size
     */
    QSize mode_size() const
    {
        return m_output->mode_size();
    }

    // TODO: The name is ambiguous. Rename this function.
    QSize pixel_size() const override
    {
        return orientate_size(m_output->mode_size());
    }

    /**
     * Describes the viewable rectangle on the output relative to the output's mode size.
     *
     * Per default the view spans the full output.
     *
     * @return viewable geometry on the output
     */
    QRect view_geometry() const
    {
        return m_view_geometry;
    }

    qreal scale() const override
    {
        // We just return the client scale here for all internal calculations depending on it (for
        // example the scaling of internal windows).
        return m_output->client_scale();
    }

    /**
     * The geometry of this output in global compositor co-ordinates (i.e scaled)
     */
    QRect geometry() const override
    {
        auto const& geo = m_output->geometry().toRect();
        // TODO: allow invalid size (disable output on the fly)
        return geo.isValid() ? geo : QRect(QPoint(0, 0), pixel_size());
    }

    QSize physical_size() const override
    {
        return orientate_size(m_output->physical_size());
    }

    /**
     * Returns the orientation of this output.
     *
     * - Flipped along the vertical axis is landscape + inv. portrait.
     * - Rotated 90° and flipped along the horizontal axis is portrait + inv. landscape
     * - Rotated 180° and flipped along the vertical axis is inv. landscape + inv. portrait
     * - Rotated 270° and flipped along the horizontal axis is inv. portrait + inv. landscape +
     *   portrait
     */
    base::wayland::output_transform transform() const
    {
        return static_cast<base::wayland::output_transform>(m_output->transform());
    }

    /**
     * Current refresh rate in 1/ms.
     */
    int refresh_rate() const override
    {
        return m_output->refresh_rate();
    }

    bool is_internal() const override
    {
        return m_internal;
    }

    void apply_changes(const Wrapland::Server::OutputChangesetV1* changeset)
    {
        auto toTransform = [](Wrapland::Server::Output::Transform transform) {
            return static_cast<base::wayland::output_transform>(transform);
        };

        qCDebug(KWIN_CORE) << "Apply changes to Wayland output:" << m_output->name().c_str();
        bool emitModeChanged = false;

        if (changeset->enabledChanged() && changeset->enabled()) {
            qCDebug(KWIN_CORE) << "Setting output enabled.";
            set_enabled(true);
        }

        if (changeset->modeChanged()) {
            qCDebug(KWIN_CORE) << "Setting new mode:" << changeset->mode();
            m_output->set_mode(changeset->mode());
            update_mode(changeset->mode());
            emitModeChanged = true;
        }
        if (changeset->transformChanged()) {
            qCDebug(KWIN_CORE) << "Server setting transform: "
                               << static_cast<int>(changeset->transform());
            m_output->set_transform(changeset->transform());
            update_transform(toTransform(changeset->transform()));
            emitModeChanged = true;
        }
        if (changeset->geometryChanged()) {
            qCDebug(KWIN_CORE) << "Server setting position: " << changeset->geometry();
            m_output->set_geometry(changeset->geometry());
            emitModeChanged = true;
        }
        update_view_geometry();

        if (changeset->enabledChanged() && !changeset->enabled()) {
            qCDebug(KWIN_CORE) << "Setting output disabled.";
            set_enabled(false);
        }

        if (emitModeChanged) {
            Q_EMIT qobject->mode_changed();
        }

        m_output->done();
    }

    Wrapland::Server::Output* wrapland_output() const
    {
        return m_output.get();
    }

    bool is_enabled() const
    {
        return m_output->enabled();
    }

    /**
     * Enable or disable the output.
     *
     * This differs from update_dpms as it also removes the wl_output.
     * The default is on.
     */
    void set_enabled(bool enable) override
    {
        m_output->set_enabled(enable);
        update_enablement(enable);
        // TODO: it is unclear that the consumer has to call done() on the output still.
    }

    void force_geometry(QRectF const& geo)
    {
        m_output->set_geometry(geo);
        update_view_geometry();
        m_output->done();
    }

    bool is_dpms_on() const override
    {
        return m_dpms == base::dpms_mode::on;
    }

    virtual uint64_t msc() const
    {
        return 0;
    }

    QSize orientate_size(QSize const& size) const
    {
        using Transform = Wrapland::Server::Output::Transform;
        auto const transform = m_output->transform();
        if (transform == Transform::Rotated90 || transform == Transform::Rotated270
            || transform == Transform::Flipped90 || transform == Transform::Flipped270) {
            return size.transposed();
        }
        return size;
    }

    using render_t = render::wayland::output<output, typename Platform::render_t>;
    std::unique_ptr<render_t> render;
    std::unique_ptr<Wrapland::Server::Output> m_output;
    base::dpms_mode m_dpms{base::dpms_mode::on};

protected:
    void init_interfaces(std::string const& name,
                         std::string const& make,
                         std::string const& model,
                         std::string const& serial_number,
                         QSize const& physical_size,
                         QVector<Wrapland::Server::Output::Mode> const& modes,
                         Wrapland::Server::Output::Mode* current_mode = nullptr)
    {

        auto from_wayland_dpms_mode = [](Wrapland::Server::Output::DpmsMode wlMode) {
            switch (wlMode) {
            case Wrapland::Server::Output::DpmsMode::On:
                return base::dpms_mode::on;
            case Wrapland::Server::Output::DpmsMode::Standby:
                return base::dpms_mode::standby;
            case Wrapland::Server::Output::DpmsMode::Suspend:
                return base::dpms_mode::suspend;
            case Wrapland::Server::Output::DpmsMode::Off:
                return base::dpms_mode::off;
            default:
                Q_UNREACHABLE();
            }
        };

        assert(!m_output);
        m_output = std::make_unique<Wrapland::Server::Output>(waylandServer()->display.get());

        m_output->set_name(name);
        m_output->set_make(make);
        m_output->set_model(model);
        m_output->set_serial_number(serial_number);
        m_output->generate_description();

        m_output->set_physical_size(physical_size);

        qCDebug(KWIN_CORE) << "Initializing output:" << m_output->description().c_str();

        int i = 0;
        for (auto mode : modes) {
            qCDebug(KWIN_CORE).nospace()
                << "Adding mode " << ++i << ": " << mode.size << " [" << mode.refresh_rate << "]";
            m_output->add_mode(mode);
        }

        if (current_mode) {
            m_output->set_mode(*current_mode);
        }

        m_output->set_geometry(QRectF(QPointF(0, 0), m_output->mode_size()));
        update_view_geometry();

        m_output->set_dpms_supported(m_supports_dpms);
        // set to last known mode
        m_output->set_dpms_mode(to_wayland_dpms_mode(m_dpms));
        QObject::connect(m_output.get(),
                         &Wrapland::Server::Output::dpms_mode_requested,
                         qobject.get(),
                         [this, from_wayland_dpms_mode](Wrapland::Server::Output::DpmsMode mode) {
                             if (!is_enabled()) {
                                 return;
                             }
                             update_dpms(from_wayland_dpms_mode(mode));
                         });

        m_output->set_enabled(true);
        m_output->done();
    }

    QPoint global_pos() const
    {
        return geometry().topLeft();
    }

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

    virtual void update_enablement(bool /*enable*/)
    {
    }

    virtual void update_mode(int /*mode_index*/)
    {
    }

    virtual void update_transform(base::wayland::output_transform /*transform*/)
    {
    }

    // TODO(romangg): the force_update variable is only a temporary solution to a larger issue, that
    // our data flow is not correctly handled between backend and this class. In general this class
    // should request data from the backend and not the backend set it.
    void set_wayland_mode(QSize const& size, int refresh_rate, bool force_update)
    {
        m_output->set_mode(size, refresh_rate);

        if (force_update) {
            m_output->done();
        }
    }

    void set_transform(base::wayland::output_transform transform)
    {
        auto to_wayland_transform = [](base::wayland::output_transform transform) {
            return static_cast<Wrapland::Server::Output::Transform>(transform);
        };

        m_output->set_transform(to_wayland_transform(transform));
        Q_EMIT qobject->mode_changed();
    }

    base::dpms_mode dpms_mode() const
    {
        return m_dpms;
    }

private:
    QSizeF logical_size() const
    {
        return geometry().size();
    }

    void update_view_geometry()
    {
        // Fit view into output mode keeping the aspect ratio.
        auto const mode_size = pixel_size();
        auto const source_size = logical_size();

        QSizeF view_size;
        view_size.setWidth(mode_size.width());
        view_size.setHeight(view_size.width() * source_size.height()
                            / static_cast<double>(source_size.width()));

        if (view_size.height() > mode_size.height()) {
            auto const oldSize = view_size;
            view_size.setHeight(mode_size.height());
            view_size.setWidth(oldSize.width() * view_size.height()
                               / static_cast<double>(oldSize.height()));
        }

        Q_ASSERT(view_size.height() <= mode_size.height());
        Q_ASSERT(view_size.width() <= mode_size.width());

        QPoint const pos((mode_size.width() - view_size.width()) / 2,
                         (mode_size.height() - view_size.height()) / 2);
        m_view_geometry = QRect(pos, view_size.toSize());
    }

    QRect m_view_geometry;

    bool m_internal = false;
    bool m_supports_dpms = false;
};

}
