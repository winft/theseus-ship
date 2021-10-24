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
#include "render/backend/wlroots/backend.h"
#include "render/compositor.h"
#include "main.h"
#include "platform.h"
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

AbstractWaylandOutput::AbstractWaylandOutput(QObject *parent)
    : AbstractOutput(parent)
{
}

QString AbstractWaylandOutput::name() const
{
    return QString::fromStdString(m_output->name());
}

QRect AbstractWaylandOutput::geometry() const
{
    const QRect &geo = m_output->geometry().toRect();
    // TODO: allow invalid size (disable output on the fly)
    return geo.isValid() ? geo : QRect(QPoint(0,0), pixelSize());
}

QSizeF AbstractWaylandOutput::logicalSize() const
{
    return geometry().size();
}

QSize AbstractWaylandOutput::physicalSize() const
{
    return orientateSize(m_output->physical_size());
}

int AbstractWaylandOutput::refreshRate() const
{
    return m_output->refresh_rate();
}

QPoint AbstractWaylandOutput::globalPos() const
{
    return geometry().topLeft();
}

void AbstractWaylandOutput::forceGeometry(const QRectF &geo)
{
    m_output->set_geometry(geo);
    updateViewGeometry();
    m_output->done();
}

QSize AbstractWaylandOutput::modeSize() const
{
    return m_output->mode_size();
}

QSize AbstractWaylandOutput::pixelSize() const
{
    return orientateSize(m_output->mode_size());
}

QRect AbstractWaylandOutput::viewGeometry() const
{
    return m_viewGeometry;
}

void AbstractWaylandOutput::updateViewGeometry()
{
    // Fit view into output mode keeping the aspect ratio.
    const QSize modeSize = pixelSize();
    const QSizeF sourceSize = logicalSize();

    QSizeF viewSize;
    viewSize.setWidth(modeSize.width());
    viewSize.setHeight(viewSize.width() * sourceSize.height() / (double)sourceSize.width());

    if (viewSize.height() > modeSize.height()) {
        auto const oldSize = viewSize;
        viewSize.setHeight(modeSize.height());
        viewSize.setWidth(oldSize.width() * viewSize.height() / (double)oldSize.height());
    }

    Q_ASSERT(viewSize.height() <= modeSize.height());
    Q_ASSERT(viewSize.width() <= modeSize.width());

    const QPoint pos((modeSize.width() - viewSize.width()) / 2,
                     (modeSize.height() - viewSize.height()) / 2);
    m_viewGeometry = QRect(pos, viewSize.toSize());
}

qreal AbstractWaylandOutput::scale() const
{
    // We just return the client scale here for all internal calculations depending on it (for
    // example the scaling of internal windows).
    return m_output->client_scale();
}

inline
base::wayland::output_transform toTransform(Wrapland::Server::Output::Transform transform)
{
    return static_cast<base::wayland::output_transform>(transform);
}

inline
Wrapland::Server::Output::Transform toWaylandTransform(base::wayland::output_transform transform)
{
    return static_cast<Wrapland::Server::Output::Transform>(transform);
}

void AbstractWaylandOutput::applyChanges(const Wrapland::Server::OutputChangesetV1 *changeset)
{
    qCDebug(KWIN_WL) << "Apply changes to Wayland output:" << m_output->name().c_str();
    bool emitModeChanged = false;

    if (changeset->enabledChanged() && changeset->enabled()) {
        qCDebug(KWIN_WL) << "Setting output enabled.";
        setEnabled(true);
    }

    if (changeset->modeChanged()) {
        qCDebug(KWIN_WL) << "Setting new mode:" << changeset->mode();
        m_output->set_mode(changeset->mode());
        updateMode(changeset->mode());
        emitModeChanged = true;
    }
    if (changeset->transformChanged()) {
        qCDebug(KWIN_WL) << "Server setting transform: " << (int)(changeset->transform());
        m_output->set_transform(changeset->transform());
        updateTransform(toTransform(changeset->transform()));
        emitModeChanged = true;
    }
    if (changeset->geometryChanged()) {
        qCDebug(KWIN_WL) << "Server setting position: " << changeset->geometry();
        m_output->set_geometry(changeset->geometry());
        emitModeChanged = true;
    }
    updateViewGeometry();

    if (changeset->enabledChanged() && !changeset->enabled()) {
        qCDebug(KWIN_WL) << "Setting output disabled.";
        setEnabled(false);
    }

    if (emitModeChanged) {
        emit modeChanged();
    }

    m_output->done();
}

bool AbstractWaylandOutput::isEnabled() const
{
    return m_output->enabled();
}

void AbstractWaylandOutput::setEnabled(bool enable)
{
    m_output->set_enabled(enable);
    updateEnablement(enable);
    // TODO: it is unclear that the consumer has to call done() on the output still.
}

// TODO(romangg): the force_update variable is only a temporary solution to a larger issue, that
// our data flow is not correctly handled between backend and this class. In general this class
// should request data from the backend and not the backend set it.
void AbstractWaylandOutput::setWaylandMode(const QSize &size, int refreshRate, bool force_update)
{
    m_output->set_mode(size, refreshRate);

    if (force_update) {
        m_output->done();
    }
}

AbstractOutput::DpmsMode fromWaylandDpmsMode(Wrapland::Server::Output::DpmsMode wlMode)
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

Wrapland::Server::Output::DpmsMode toWaylandDpmsMode(AbstractOutput::DpmsMode mode)
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

void AbstractWaylandOutput::initInterfaces(std::string const& name,
                                           std::string const& make,
                                           std::string const& model,
                                           std::string const& serial_number,
                                           const QSize &physicalSize,
                                           const QVector<Wrapland::Server::Output::Mode> &modes,
                                           Wrapland::Server::Output::Mode *current_mode)
{
    Q_ASSERT(!m_output);
    m_output = std::make_unique<Wrapland::Server::Output>(waylandServer()->display());

    m_output->set_name(name);
    m_output->set_make(make);
    m_output->set_model(model);
    m_output->set_serial_number(serial_number);
    m_output->generate_description();

    m_output->set_physical_size(physicalSize);

    qCDebug(KWIN_WL) << "Initializing output:" << m_output->description().c_str();

    int i = 0;
    for (auto mode : modes) {
        qCDebug(KWIN_WL).nospace() << "Adding mode " << ++i << ": "
                                     << mode.size << " [" << mode.refresh_rate << "]";
        m_output->add_mode(mode);
    }

    if (current_mode) {
        m_output->set_mode(*current_mode);
    }

    m_output->set_geometry(QRectF(QPointF(0, 0), m_output->mode_size()));
    updateViewGeometry();

    m_output->set_dpms_supported(m_supportsDpms);
    // set to last known mode
    m_output->set_dpms_mode(toWaylandDpmsMode(m_dpms));
    connect(m_output.get(), &Wrapland::Server::Output::dpms_mode_requested, this,
        [this] (Wrapland::Server::Output::DpmsMode mode) {
            if (!isEnabled()) {
                return;
            }
            updateDpms(fromWaylandDpmsMode(mode));
        }
    );

    m_output->set_enabled(true);
    m_output->done();
}

QSize AbstractWaylandOutput::orientateSize(const QSize &size) const
{
    using Transform = Wrapland::Server::Output::Transform;
    const Transform transform = m_output->transform();
    if (transform == Transform::Rotated90 || transform == Transform::Rotated270 ||
            transform == Transform::Flipped90 || transform == Transform::Flipped270) {
        return size.transposed();
    }
    return size;
}

void AbstractWaylandOutput::setTransform(base::wayland::output_transform transform)
{
    m_output->set_transform(toWaylandTransform(transform));
    emit modeChanged();
}

base::wayland::output_transform AbstractWaylandOutput::transform() const
{
    return static_cast<base::wayland::output_transform>(m_output->transform());
}

void AbstractWaylandOutput::dpmsSetOn()
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << name() << "to On.";
    m_dpms = DpmsMode::On;

    if (isEnabled()) {
        m_output->set_dpms_mode(Wrapland::Server::Output::DpmsMode::On);
    }

    auto& wlroots_base = static_cast<ApplicationWaylandAbstract*>(kwinApp())->get_base();
    auto wayland_input = static_cast<input::wayland::platform*>(kwinApp()->input.get());
    base::wayland::check_outputs_on(wlroots_base, wayland_input->dpms_filter);

    if (auto compositor = render::compositor::self()) {
        compositor->addRepaintFull();
    }
}

void AbstractWaylandOutput::dpmsSetOff(DpmsMode mode)
{
    qCDebug(KWIN_WL) << "DPMS mode set for output" << name() << "to Off.";

    m_dpms = mode;

    if (isEnabled()) {
        m_output->set_dpms_mode(toWaylandDpmsMode(mode));

        auto wayland_input = static_cast<input::wayland::platform*>(kwinApp()->input.get());
        input::wayland::create_dpms_filter(wayland_input);
    }
}

AbstractWaylandOutput::DpmsMode AbstractWaylandOutput::dpmsMode() const
{
    return m_dpms;
}

bool AbstractWaylandOutput::dpmsOn() const
{
    return m_dpms == DpmsMode::On;
}

uint64_t AbstractWaylandOutput::msc() const
{
    return 0;
}

}
