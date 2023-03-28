/*
SPDX-FileCopyrightText: 2019, 2021 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
#include <memory>

namespace KWin::base::wayland
{

template<typename Platform>
class output : public base::output
{
public:
    output(Platform& platform)
        : platform{platform}
    {
    }

    QString name() const override
    {
        return QString::fromStdString(m_output->get_metadata().name);
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
        return m_output->get_state().mode.size;
    }

    // TODO: The name is ambiguous. Rename this function.
    QSize pixel_size() const override
    {
        return orientate_size(m_output->get_state().mode.size);
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
        return m_output->get_state().client_scale;
    }

    /**
     * The geometry of this output in global compositor co-ordinates (i.e scaled)
     */
    QRect geometry() const override
    {
        auto const& geo = m_output->get_state().geometry.toRect();
        // TODO: allow invalid size (disable output on the fly)
        return geo.isValid() ? geo : QRect(QPoint(0, 0), pixel_size());
    }

    QSize physical_size() const override
    {
        return orientate_size(m_output->get_metadata().physical_size);
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
        return static_cast<base::wayland::output_transform>(m_output->get_state().transform);
    }

    /**
     * Current refresh rate in 1/ms.
     */
    int refresh_rate() const override
    {
        return m_output->get_state().mode.refresh_rate;
    }

    bool is_internal() const override
    {
        return m_internal;
    }

    bool apply_state(Wrapland::Server::output_state const& state)
    {
        qCDebug(KWIN_CORE) << "Apply changes to Wayland output:"
                           << m_output->get_metadata().name.c_str();

        if (!change_backend_state(state)) {
            return false;
        }

        m_output->set_state(state);
        update_view_geometry();
        return true;
    }

    Wrapland::Server::output* wrapland_output() const
    {
        return m_output.get();
    }

    bool is_enabled() const
    {
        return m_output->get_state().enabled;
    }

    void force_geometry(QRectF const& geo)
    {
        auto state = m_output->get_state();
        state.geometry = geo;
        m_output->set_state(state);
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
        using Transform = Wrapland::Server::output_transform;
        auto const transform = m_output->get_state().transform;
        if (transform == Transform::rotated_90 || transform == Transform::rotated_270
            || transform == Transform::flipped_90 || transform == Transform::flipped_270) {
            return size.transposed();
        }
        return size;
    }

    virtual bool change_backend_state(Wrapland::Server::output_state const& state) = 0;

    using render_t = render::wayland::output<output, typename Platform::render_t>;
    std::unique_ptr<render_t> render;
    std::unique_ptr<Wrapland::Server::output> m_output;
    base::dpms_mode m_dpms{base::dpms_mode::on};
    Platform& platform;

protected:
    void init_interfaces(std::string const& name,
                         std::string const& make,
                         std::string const& model,
                         std::string const& serial_number,
                         QSize const& physical_size,
                         QVector<Wrapland::Server::output_mode> const& modes,
                         Wrapland::Server::output_mode* current_mode = nullptr)
    {

        auto from_wayland_dpms_mode = [](auto wlMode) {
            switch (wlMode) {
            case Wrapland::Server::output_dpms_mode::on:
                return base::dpms_mode::on;
            case Wrapland::Server::output_dpms_mode::standby:
                return base::dpms_mode::standby;
            case Wrapland::Server::output_dpms_mode::suspend:
                return base::dpms_mode::suspend;
            case Wrapland::Server::output_dpms_mode::off:
                return base::dpms_mode::off;
            default:
                Q_UNREACHABLE();
            }
        };

        Wrapland::Server::output_metadata metadata{
            .name = name,
            .make = make,
            .model = model,
            .serial_number = serial_number,
            .physical_size = physical_size,
        };

        assert(!m_output);
        m_output = std::make_unique<Wrapland::Server::output>(metadata,
                                                              *platform.server->output_manager);

        qCDebug(KWIN_CORE) << "Initializing output:"
                           << m_output->get_metadata().description.c_str();

        int i = 0;
        for (auto mode : modes) {
            qCDebug(KWIN_CORE).nospace()
                << "Adding mode " << ++i << ": " << mode.size << " [" << mode.refresh_rate << "]";
            m_output->add_mode(mode);
        }

        auto state = m_output->get_state();
        if (current_mode) {
            state.mode = *current_mode;
        }

        state.enabled = true;
        state.geometry = QRectF(QPointF(0, 0), state.mode.size);

        m_output->set_state(state);
        update_view_geometry();

        m_output->set_dpms_supported(m_supports_dpms);

        // set to last known mode
        m_output->set_dpms_mode(to_wayland_dpms_mode(m_dpms));
        QObject::connect(m_output.get(),
                         &Wrapland::Server::output::dpms_mode_requested,
                         qobject.get(),
                         [this, from_wayland_dpms_mode](auto mode) {
                             if (!is_enabled()) {
                                 return;
                             }
                             update_dpms(from_wayland_dpms_mode(mode));
                         });

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
