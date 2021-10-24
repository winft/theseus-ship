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
#include "abstract_wayland_output.h"

#include "base/wayland/output_helpers.h"
#include "input/wayland/dpms.h"
#include "input/wayland/platform.h"
#include "main.h"
#include "platform.h"
#include "render/backend/wlroots/backend.h"
#include "render/compositor.h"
#include "screens.h"
#include "wayland_logging.h"
#include "wayland_server.h"

// Wrapland
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/output_changeset_v1.h>
#include <Wrapland/Server/xdg_output.h>
// KF5
#include <KLocalizedString>

#include <cmath>

namespace KWin
{

QString AbstractWaylandOutput::name() const
{
    return QString::fromStdString(m_output->name());
}

QRect AbstractWaylandOutput::geometry() const
{
    auto const& geo = m_output->geometry().toRect();
    // TODO: allow invalid size (disable output on the fly)
    return geo.isValid() ? geo : QRect(QPoint(0, 0), pixel_size());
}

QSizeF AbstractWaylandOutput::logical_size() const
{
    return geometry().size();
}

QSize AbstractWaylandOutput::physical_size() const
{
    return orientate_size(m_output->physical_size());
}

int AbstractWaylandOutput::refresh_rate() const
{
    return m_output->refresh_rate();
}

QPoint AbstractWaylandOutput::global_pos() const
{
    return geometry().topLeft();
}

void AbstractWaylandOutput::force_geometry(QRectF const& geo)
{
    m_output->set_geometry(geo);
    update_view_geometry();
    m_output->done();
}

QSize AbstractWaylandOutput::mode_size() const
{
    return m_output->mode_size();
}

QSize AbstractWaylandOutput::pixel_size() const
{
    return orientate_size(m_output->mode_size());
}

QRect AbstractWaylandOutput::view_geometry() const
{
    return m_view_geometry;
}

void AbstractWaylandOutput::update_view_geometry()
{
    // Fit view into output mode keeping the aspect ratio.
    auto const mode_size = pixel_size();
    auto const source_size = logical_size();

    QSizeF view_size;
    view_size.setWidth(mode_size.width());
    view_size.setHeight(view_size.width() * source_size.height() / (double)source_size.width());

    if (view_size.height() > mode_size.height()) {
        auto const oldSize = view_size;
        view_size.setHeight(mode_size.height());
        view_size.setWidth(oldSize.width() * view_size.height() / (double)oldSize.height());
    }

    Q_ASSERT(view_size.height() <= mode_size.height());
    Q_ASSERT(view_size.width() <= mode_size.width());

    QPoint const pos((mode_size.width() - view_size.width()) / 2,
                     (mode_size.height() - view_size.height()) / 2);
    m_view_geometry = QRect(pos, view_size.toSize());
}

qreal AbstractWaylandOutput::scale() const
{
    // We just return the client scale here for all internal calculations depending on it (for
    // example the scaling of internal windows).
    return m_output->client_scale();
}

inline base::wayland::output_transform toTransform(Wrapland::Server::Output::Transform transform)
{
    return static_cast<base::wayland::output_transform>(transform);
}

inline Wrapland::Server::Output::Transform
to_wayland_transform(base::wayland::output_transform transform)
{
    return static_cast<Wrapland::Server::Output::Transform>(transform);
}

void AbstractWaylandOutput::apply_changes(Wrapland::Server::OutputChangesetV1 const* changeset)
{
    qCDebug(KWIN_WL) << "Apply changes to Wayland output:" << m_output->name().c_str();
    bool emitModeChanged = false;

    if (changeset->enabledChanged() && changeset->enabled()) {
        qCDebug(KWIN_WL) << "Setting output enabled.";
        set_enabled(true);
    }

    if (changeset->modeChanged()) {
        qCDebug(KWIN_WL) << "Setting new mode:" << changeset->mode();
        m_output->set_mode(changeset->mode());
        update_mode(changeset->mode());
        emitModeChanged = true;
    }
    if (changeset->transformChanged()) {
        qCDebug(KWIN_WL) << "Server setting transform: " << (int)(changeset->transform());
        m_output->set_transform(changeset->transform());
        update_transform(toTransform(changeset->transform()));
        emitModeChanged = true;
    }
    if (changeset->geometryChanged()) {
        qCDebug(KWIN_WL) << "Server setting position: " << changeset->geometry();
        m_output->set_geometry(changeset->geometry());
        emitModeChanged = true;
    }
    update_view_geometry();

    if (changeset->enabledChanged() && !changeset->enabled()) {
        qCDebug(KWIN_WL) << "Setting output disabled.";
        set_enabled(false);
    }

    if (emitModeChanged) {
        Q_EMIT mode_changed();
    }

    m_output->done();
}

bool AbstractWaylandOutput::is_enabled() const
{
    return m_output->enabled();
}

void AbstractWaylandOutput::set_enabled(bool enable)
{
    m_output->set_enabled(enable);
    update_enablement(enable);
    // TODO: it is unclear that the consumer has to call done() on the output still.
}

// TODO(romangg): the force_update variable is only a temporary solution to a larger issue, that
// our data flow is not correctly handled between backend and this class. In general this class
// should request data from the backend and not the backend set it.
void AbstractWaylandOutput::set_wayland_mode(QSize const& size, int refresh_rate, bool force_update)
{
    m_output->set_mode(size, refresh_rate);

    if (force_update) {
        m_output->done();
    }
}

AbstractOutput::DpmsMode from_wayland_dpms_mode(Wrapland::Server::Output::DpmsMode wlMode)
{
    switch (wlMode) {
    case Wrapland::Server::Output::DpmsMode::On:
        return AbstractOutput::DpmsMode::On;
    case Wrapland::Server::Output::DpmsMode::Standby:
        return AbstractOutput::DpmsMode::Standby;
    case Wrapland::Server::Output::DpmsMode::Suspend:
        return AbstractOutput::DpmsMode::Suspend;
    case Wrapland::Server::Output::DpmsMode::Off:
        return AbstractOutput::DpmsMode::Off;
    default:
        Q_UNREACHABLE();
    }
}

Wrapland::Server::Output::DpmsMode to_wayland_dpms_mode(AbstractOutput::DpmsMode mode)
{
    switch (mode) {
    case AbstractOutput::DpmsMode::On:
        return Wrapland::Server::Output::DpmsMode::On;
    case AbstractOutput::DpmsMode::Standby:
        return Wrapland::Server::Output::DpmsMode::Standby;
    case AbstractOutput::DpmsMode::Suspend:
        return Wrapland::Server::Output::DpmsMode::Suspend;
    case AbstractOutput::DpmsMode::Off:
        return Wrapland::Server::Output::DpmsMode::Off;
    default:
        Q_UNREACHABLE();
    }
}

void AbstractWaylandOutput::init_interfaces(std::string const& name,
                                            std::string const& make,
                                            std::string const& model,
                                            std::string const& serial_number,
                                            QSize const& physical_size,
                                            QVector<Wrapland::Server::Output::Mode> const& modes,
                                            Wrapland::Server::Output::Mode* current_mode)
{
    Q_ASSERT(!m_output);
    m_output = std::make_unique<Wrapland::Server::Output>(waylandServer()->display());

    m_output->set_name(name);
    m_output->set_make(make);
    m_output->set_model(model);
    m_output->set_serial_number(serial_number);
    m_output->generate_description();

    m_output->set_physical_size(physical_size);

    qCDebug(KWIN_WL) << "Initializing output:" << m_output->description().c_str();

    int i = 0;
    for (auto mode : modes) {
        qCDebug(KWIN_WL).nospace()
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
    connect(m_output.get(),
            &Wrapland::Server::Output::dpms_mode_requested,
            this,
            [this](Wrapland::Server::Output::DpmsMode mode) {
                if (!is_enabled()) {
                    return;
                }
                update_dpms(from_wayland_dpms_mode(mode));
            });

    m_output->set_enabled(true);
    m_output->done();
}

QSize AbstractWaylandOutput::orientate_size(QSize const& size) const
{
    using Transform = Wrapland::Server::Output::Transform;
    auto const transform = m_output->transform();
    if (transform == Transform::Rotated90 || transform == Transform::Rotated270
        || transform == Transform::Flipped90 || transform == Transform::Flipped270) {
        return size.transposed();
    }
    return size;
}

void AbstractWaylandOutput::set_transform(base::wayland::output_transform transform)
{
    m_output->set_transform(to_wayland_transform(transform));
    Q_EMIT mode_changed();
}

base::wayland::output_transform AbstractWaylandOutput::transform() const
{
    return static_cast<base::wayland::output_transform>(m_output->transform());
}

void AbstractWaylandOutput::dpms_set_on()
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << name() << "to On.";
    m_dpms = DpmsMode::On;

    if (is_enabled()) {
        m_output->set_dpms_mode(Wrapland::Server::Output::DpmsMode::On);
    }

    auto& wlroots_base = static_cast<ApplicationWaylandAbstract*>(kwinApp())->get_base();
    auto wayland_input = static_cast<input::wayland::platform*>(kwinApp()->input.get());
    base::wayland::check_outputs_on(wlroots_base, wayland_input->dpms_filter);

    if (auto compositor = render::compositor::self()) {
        compositor->addRepaintFull();
    }
}

void AbstractWaylandOutput::dpms_set_off(DpmsMode mode)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << name() << "to Off.";

    m_dpms = mode;

    if (is_enabled()) {
        m_output->set_dpms_mode(to_wayland_dpms_mode(mode));

        auto wayland_input = static_cast<input::wayland::platform*>(kwinApp()->input.get());
        input::wayland::create_dpms_filter(wayland_input);
    }
}

AbstractWaylandOutput::DpmsMode AbstractWaylandOutput::dpms_mode() const
{
    return m_dpms;
}

bool AbstractWaylandOutput::is_dpms_on() const
{
    return m_dpms == DpmsMode::On;
}

uint64_t AbstractWaylandOutput::msc() const
{
    return 0;
}

}
