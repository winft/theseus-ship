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

#include "screens.h"
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

AbstractWaylandOutput::~AbstractWaylandOutput()
{
    delete m_xdgOutput.data();
    delete m_waylandOutput.data();
    delete m_waylandOutputDevice.data();
}

QString AbstractWaylandOutput::name() const
{
    return QStringLiteral("%1 %2").arg(m_waylandOutputDevice->manufacturer()).arg(
                m_waylandOutputDevice->model());
}

QByteArray AbstractWaylandOutput::uuid() const
{
    return m_waylandOutputDevice->uuid();
}

QRect AbstractWaylandOutput::geometry() const
{
    const QRect &geo = m_waylandOutputDevice->geometry().toRect();
    // TODO: allow invalid size (disable output on the fly)
    return geo.isValid() ? geo : QRect(QPoint(0,0), pixelSize());
}

QSizeF AbstractWaylandOutput::logicalSize() const
{
    return geometry().size();
}

int AbstractWaylandOutput::clientScale() const
{
    const QSizeF &size = logicalSize();
    const QSizeF &modeSize = pixelSize();

    const qreal widthRatio = modeSize.width() / size.width();
    const qreal heightRatio = modeSize.height() / size.height();

    return std::ceil(std::max(widthRatio, heightRatio));
}

QSize AbstractWaylandOutput::physicalSize() const
{
    return orientateSize(m_waylandOutputDevice->physicalSize());
}

int AbstractWaylandOutput::refreshRate() const
{
    return m_waylandOutputDevice->refreshRate();
}

QPoint AbstractWaylandOutput::globalPos() const
{
    return geometry().topLeft();
}

void AbstractWaylandOutput::setGeometry(const QRectF &geo)
{
    m_waylandOutputDevice->setGeometry(geo);

    if (isEnabled()) {
        const QPoint pos = geo.topLeft().toPoint();

        m_waylandOutput->setGlobalPosition(pos);
        m_xdgOutput->setLogicalPosition(pos);
        m_xdgOutput->setLogicalSize(geo.size().toSize());
        m_xdgOutput->done();
    }
}

void AbstractWaylandOutput::forceGeometry(const QRectF &geo)
{
    setGeometry(geo);
    updateViewGeometry();
    setWaylandOutputScale();
}

QSize AbstractWaylandOutput::modeSize() const
{
    return m_waylandOutputDevice->modeSize();
}

QSize AbstractWaylandOutput::pixelSize() const
{
    return orientateSize(m_waylandOutputDevice->modeSize());
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

    QSize viewSize;
    viewSize.setWidth(modeSize.width());
    viewSize.setHeight(viewSize.width() * sourceSize.height() / (double)sourceSize.width());

    if (viewSize.height() > modeSize.height()) {
        const QSize oldSize = viewSize;
        viewSize.setHeight(modeSize.height());
        viewSize.setWidth(oldSize.width() * viewSize.height() / (double)oldSize.height());
    }

    Q_ASSERT(viewSize.height() <= modeSize.height());
    Q_ASSERT(viewSize.width() <= modeSize.width());

    const QPoint pos((modeSize.width() - viewSize.width()) / 2,
                     (modeSize.height() - viewSize.height()) / 2);
    m_viewGeometry = QRect(pos, viewSize);
}

qreal AbstractWaylandOutput::scale() const
{
    // We just return the clientScale here for all internal calculations depending on it (for
    // example the scaling of internal windows).
    return clientScale();
}

void AbstractWaylandOutput::setWaylandOutputScale()
{
    if (isEnabled()) {
        m_waylandOutput->setScale(clientScale());

        // TODO: We set this here as well, because it is not clear how well XWayland reacts at the
        //       moment when only the Wayland output sends a done event. Wait for xdg-output-v3 to
        //       get this fixed or make it explicit as with xdg-output and output device.
        m_xdgOutput->setLogicalSize(logicalSize().toSize());
        m_xdgOutput->done();
    }
}

using Device = Wrapland::Server::OutputDeviceV1;

Wrapland::Server::Output::Transform toOutputTransform(Device::Transform transform)
{
    using Transform = Device::Transform;
    using OutputTransform = Wrapland::Server::Output::Transform;

    switch (transform) {
    case Transform::Rotated90:
        return OutputTransform::Rotated90;
    case Transform::Rotated180:
        return OutputTransform::Rotated180;
    case Transform::Rotated270:
        return OutputTransform::Rotated270;
    case Transform::Flipped:
        return OutputTransform::Flipped;
    case Transform::Flipped90:
        return OutputTransform::Flipped90;
    case Transform::Flipped180:
        return OutputTransform::Flipped180;
    case Transform::Flipped270:
        return OutputTransform::Flipped270;
    default:
        return OutputTransform::Normal;
    }
}

void AbstractWaylandOutput::setTransform(Device::Transform transform)
{
    m_waylandOutputDevice->setTransform(transform);

    if (isEnabled()) {
        m_waylandOutput->setTransform(toOutputTransform(transform));
        m_xdgOutput->setLogicalSize(logicalSize().toSize());
        m_xdgOutput->done();
    }
}

inline
AbstractWaylandOutput::Transform toTransform(Device::Transform deviceTransform)
{
    return static_cast<AbstractWaylandOutput::Transform>(deviceTransform);
}

inline
Device::Transform toDeviceTransform(AbstractWaylandOutput::Transform transform)
{
    return static_cast<Device::Transform>(transform);
}

void AbstractWaylandOutput::applyChanges(const Wrapland::Server::OutputChangesetV1 *changeset)
{
    qCDebug(KWIN_CORE) << "Apply changes to the Wayland output.";
    bool emitModeChanged = false;

    // Enablement changes are handled by platform.
    if (changeset->modeChanged()) {
        qCDebug(KWIN_CORE) << "Setting new mode:" << changeset->mode();
        m_waylandOutputDevice->setMode(changeset->mode());
        updateMode(changeset->mode());
        emitModeChanged = true;
    }
    if (changeset->transformChanged()) {
        qCDebug(KWIN_CORE) << "Server setting transform: " << (int)(changeset->transform());
        setTransform(changeset->transform());
        updateTransform(toTransform(changeset->transform()));
        emitModeChanged = true;
    }
    if (changeset->geometryChanged()) {
        qCDebug(KWIN_CORE) << "Server setting position: " << changeset->geometry();
        setGeometry(changeset->geometry());
        emitModeChanged = true;
        // may just work already!
    }
    updateViewGeometry();

    if (emitModeChanged) {
        setWaylandOutputScale();
        emit modeChanged();

        // Send the screens changed signal extra because the position might be changed
        // without the mode size.
        // TODO: make this right when Screens class is finally removed.
        emit screens()->changed();
    }
    if (changeset->enabled() == Wrapland::Server::OutputDeviceV1::Enablement::Enabled) {
        m_waylandOutputDevice->done();
    }
}

bool AbstractWaylandOutput::isEnabled() const
{
    return m_waylandOutputDevice->enabled() == Device::Enablement::Enabled;
}

void AbstractWaylandOutput::setEnabled(bool enable)
{
    if (enable == isEnabled()) {
        return;
    }

    if (enable) {
        waylandOutputDevice()->setEnabled(Device::Enablement::Enabled);
        createWaylandOutput();
        updateEnablement(true);
    } else {
        waylandOutputDevice()->setEnabled(Device::Enablement::Disabled);
        // xdg-output is destroyed in Wrapland on wl_output going away.
        delete m_waylandOutput.data();
        updateEnablement(false);

        // TODO: When an outputs gets disabled we directly broadcast to all clients (compare
        //       Platform::requestOutputsChange). Can we combine disabling and changing an output
        //       instead?
        m_waylandOutputDevice->done();
    }
}

void AbstractWaylandOutput::setWaylandMode(const QSize &size, int refreshRate)
{
    if (!isEnabled()) {
        return;
    }
    m_waylandOutput->setCurrentMode(size, refreshRate);
    m_xdgOutput->setLogicalSize(logicalSize().toSize());
    m_xdgOutput->done();
}

void AbstractWaylandOutput::createXdgOutput()
{
    Q_ASSERT(!m_waylandOutput.isNull());
    Q_ASSERT(m_xdgOutput.isNull());

    m_xdgOutput = waylandServer()->xdgOutputManager()->createXdgOutput(m_waylandOutput, m_waylandOutput);
    m_xdgOutput->setLogicalSize(logicalSize().toSize());
    m_xdgOutput->setLogicalPosition(globalPos());
    m_xdgOutput->done();
}

void AbstractWaylandOutput::createWaylandOutput()
{
    Q_ASSERT(m_waylandOutput.isNull());
    m_waylandOutput = waylandServer()->display()->createOutput();
    createXdgOutput();

    /*
     *  add base wayland output data
     */
    m_waylandOutput->setManufacturer(m_waylandOutputDevice->manufacturer().toUtf8().constData());
    m_waylandOutput->setModel(m_waylandOutputDevice->model().toUtf8().constData());
    m_waylandOutput->setPhysicalSize(m_waylandOutputDevice->physicalSize());
    m_waylandOutput->setScale(clientScale());

    /*
     *  add modes
     */
    for(const auto &mode: m_waylandOutputDevice->modes()) {
        Wrapland::Server::Output::ModeFlags flags;
        if (mode.flags & Device::ModeFlag::Current) {
            flags |= Wrapland::Server::Output::ModeFlag::Current;
        }
        if (mode.flags & Device::ModeFlag::Preferred) {
            flags |= Wrapland::Server::Output::ModeFlag::Preferred;
        }
        m_waylandOutput->addMode(mode.size, flags, mode.refreshRate);
    }

    /*
     *  set dpms
     */
    m_waylandOutput->setDpmsSupported(m_supportsDpms);
    // set to last known mode
    m_waylandOutput->setDpmsMode(m_dpms);
    connect(m_waylandOutput.data(), &Wrapland::Server::Output::dpmsModeRequested, this,
        [this] (Wrapland::Server::Output::DpmsMode mode) {
            updateDpms(mode);
        }
    );
}

void AbstractWaylandOutput::initInterfaces(const QString &model, const QString &manufacturer,
                                           const QByteArray &uuid, const QSize &physicalSize,
                                           const QVector<Device::Mode> &modes)
{
    Q_ASSERT(m_waylandOutputDevice.isNull());
    m_waylandOutputDevice = waylandServer()->display()->createOutputDeviceV1();
    m_waylandOutputDevice->setUuid(uuid);

    if (!manufacturer.isEmpty()) {
        m_waylandOutputDevice->setManufacturer(manufacturer);
    } else {
        m_waylandOutputDevice->setManufacturer(i18n("unknown"));
    }

    m_waylandOutputDevice->setModel(model);
    m_waylandOutputDevice->setPhysicalSize(physicalSize);

    int i = 0;
    for (auto mode : modes) {
        qCDebug(KWIN_CORE).nospace() << "Adding mode " << ++i << ": " << mode.size << " [" << mode.refreshRate << "]";
        m_waylandOutputDevice->addMode(mode);
    }

    m_waylandOutputDevice->setGeometry(QRectF(QPointF(0, 0), m_waylandOutputDevice->modeSize()));
    updateViewGeometry();

    m_waylandOutputDevice->done();

    createWaylandOutput();
}

QSize AbstractWaylandOutput::orientateSize(const QSize &size) const
{
    using Transform = Device::Transform;
    const Transform transform = m_waylandOutputDevice->transform();
    if (transform == Transform::Rotated90 || transform == Transform::Rotated270 ||
            transform == Transform::Flipped90 || transform == Transform::Flipped270) {
        return size.transposed();
    }
    return size;
}

void AbstractWaylandOutput::setTransform(Transform transform)
{
    const auto deviceTransform = toDeviceTransform(transform);
    if (deviceTransform == m_waylandOutputDevice->transform()) {
        return;
    }
    setTransform(deviceTransform);
    emit modeChanged();
}

AbstractWaylandOutput::Transform AbstractWaylandOutput::transform() const
{
    return static_cast<Transform>(m_waylandOutputDevice->transform());
}

}
