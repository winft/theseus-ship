/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "drm_backend.h"
#include "drm_output.h"
#include "drm_object_connector.h"
#include "drm_object_crtc.h"
#include "drm_object_plane.h"
#include "composite.h"
#include "cursor.h"
#include "logging.h"
#include "logind.h"
#include "main.h"
#include "scene_qpainter_drm_backend.h"
#include "screens_drm.h"
#include "udev.h"
#include "wayland_server.h"
#if HAVE_GBM
#include "egl_gbm_backend.h"
#include <gbm.h>
#endif
#if HAVE_EGL_STREAMS
#include "egl_stream_backend.h"
#endif
// Wrapland
#include <Wrapland/Server/seat.h>
// KF5
#include <KConfigGroup>
#include <KCoreAddons>
#include <KLocalizedString>
#include <KSharedConfig>
// Qt
#include <QCryptographicHash>
#include <QSocketNotifier>
#include <QPainter>
// system
#include <algorithm>
#include <unistd.h>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_mode.h>

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif

#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

#define KWIN_DRM_EVENT_CONTEXT_VERSION 3

namespace KWin
{

DrmBackend::DrmBackend(QObject *parent)
    : Platform(parent)
    , m_udev(new Udev)
    , m_udevMonitor(m_udev->monitor())
    , m_dpmsFilter()
{
#if HAVE_EGL_STREAMS
    if (qEnvironmentVariableIsSet("KWIN_DRM_USE_EGL_STREAMS")) {
        m_useEglStreams = true;
    }
#endif
    setSupportsGammaControl(true);
    supportsOutputChanges();
}

DrmBackend::~DrmBackend()
{
#if HAVE_GBM
    if (m_gbmDevice) {
        gbm_device_destroy(m_gbmDevice);
    }
#endif
    if (m_fd >= 0) {
        // wait for pageflips
        while (m_pageFlipsPending != 0) {
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
        }

        qDeleteAll(m_planes);
        qDeleteAll(m_crtcs);
        qDeleteAll(m_connectors);
        close(m_fd);
    }
}

void DrmBackend::init()
{
    LogindIntegration *logind = LogindIntegration::self();
    auto takeControl = [logind, this]() {
        if (logind->hasSessionControl()) {
            openDrm();
        } else {
            logind->takeControl();
            connect(logind, &LogindIntegration::hasSessionControlChanged, this, &DrmBackend::openDrm);
        }
    };
    if (logind->isConnected()) {
        takeControl();
    } else {
        connect(logind, &LogindIntegration::connectedChanged, this, takeControl);
    }
}

void DrmBackend::prepareShutdown()
{
    writeOutputsConfiguration();
    for (DrmOutput *output : m_outputs) {
        output->teardown();
    }
    Platform::prepareShutdown();
}

Outputs DrmBackend::outputs() const
{
    return m_outputs;
}

Outputs DrmBackend::enabledOutputs() const
{
    return m_enabledOutputs;
}

void DrmBackend::createDpmsFilter()
{
    if (!m_dpmsFilter.isNull()) {
        // already another output is off
        return;
    }
    m_dpmsFilter.reset(new DpmsInputEventFilter(this));
    input()->prependInputEventFilter(m_dpmsFilter.data());
}

void DrmBackend::turnOutputsOn()
{
    m_dpmsFilter.reset();
    for (auto it = m_enabledOutputs.constBegin(), end = m_enabledOutputs.constEnd(); it != end; it++) {
        (*it)->updateDpms(Wrapland::Server::Output::DpmsMode::On);
    }
}

void DrmBackend::checkOutputsAreOn()
{
    if (m_dpmsFilter.isNull()) {
        // already disabled, all outputs are on
        return;
    }
    for (auto it = m_enabledOutputs.constBegin(), end = m_enabledOutputs.constEnd(); it != end; it++) {
        if (!(*it)->isDpmsEnabled()) {
            // dpms still disabled, need to keep the filter
            return;
        }
    }
    // all outputs are on, disable the filter
    m_dpmsFilter.reset();
}

void DrmBackend::activate(bool active)
{
    if (active) {
        qCDebug(KWIN_DRM) << "Activating session.";
        reactivate();
    } else {
        qCDebug(KWIN_DRM) << "Deactivating session.";
        deactivate();
    }
}

void DrmBackend::reactivate()
{
    if (m_active) {
        return;
    }
    m_active = true;
    if (!usesSoftwareCursor()) {
        const QPoint cp = Cursor::pos() - softwareCursorHotspot();
        for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
            DrmOutput *o = *it;
            // only relevant in atomic mode
            o->m_modesetRequested = true;
            o->m_crtc->blank();
            o->showCursor();
            o->moveCursor(cp);
        }
    }
    // restart compositor
    m_pageFlipsPending = 0;
    if (Compositor *compositor = Compositor::self()) {
        compositor->bufferSwapComplete();
        compositor->addRepaintFull();
    }
}

void DrmBackend::deactivate()
{
    if (!m_active) {
        return;
    }
    // block compositor
    if (m_pageFlipsPending == 0 && Compositor::self()) {
        Compositor::self()->aboutToSwapBuffers();
    }
    // hide cursor and disable
    for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
        DrmOutput *o = *it;
        o->hideCursor();
    }
    m_active = false;
}

// This is currently just a copy of the legacyFlipHandler. At the moment we do not handle multi-crtc
// flipping in a well-defined way, we just atomic-commit for each output one after the other and not
// synced with each output's individual vblank interval (think outputs with different refresh
// rates).
//
// Looking into synchronized flips per output at some point we probably need this function if a
// single atomic commit is changing multiple outputs simultaneously. The question is why do we want
// this at all if we need to do individual flips anyway. Is it beneficial to do one commit only?
// Maybe performance is better.
//
// And let's assume if you have two 60Hz outputs their crtcs are synchronized. Then a single atomic
// commit is always better than two. If you have a 60Hz and a 120Hz output assuming the crtc are
// also synchronized such that every second frame of the 120Hz output alignes with the 60Hz output
// and you can use a single commit for this second frame and another one with changes only for the
// 120Hz output.
//
// Assuming crtc refresh cycles are not synchronized and vblank happens at random times then with
// a delay till shortly before vblank we could still sometimes synchronize with some tolerance if
// the vblanks are relatively close to each other (for example wait 2ms before vblank instead of
// "optimal" 1ms).
//
// TLDR: this function makes in any case sense if output refresh rates are synchronized on crtc
//       level, otherwise if we wait till shortly before vblank for each crtc we can still
//       sometimes synchronize the commit for multiple outputs if we do not wait instead till
//       "optimal" time shortly before vblank for every output.
void DrmBackend::atomicFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
                                   unsigned int crtc_id, void *data)
{
    Q_UNUSED(fd)
    Q_UNUSED(frame)
    Q_UNUSED(sec)
    Q_UNUSED(usec)
    Q_UNUSED(crtc_id)

    auto *output = reinterpret_cast<DrmOutput*>(data);
    output->pageFlipped();
    output->m_backend->m_pageFlipsPending--;

    if (output->m_backend->m_pageFlipsPending == 0) {
        // TODO: improve, this currently means we wait for all page flips or all outputs.
        // It would be better to driver the repaint per output

        if (Compositor::self()) {
            Compositor::self()->bufferSwapComplete();
        }
    }
}

void DrmBackend::legacyFlipHandler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
                                   void *data)
{
    Q_UNUSED(fd)
    Q_UNUSED(frame)
    Q_UNUSED(sec)
    Q_UNUSED(usec)

    auto output = reinterpret_cast<DrmOutput*>(data);
    output->pageFlipped();
    output->m_backend->m_pageFlipsPending--;

    if (output->m_backend->m_pageFlipsPending == 0) {
        // TODO: improve, this currently means we wait for all page flips or all outputs.
        // It would be better to driver the repaint per output

        if (Compositor::self()) {
            Compositor::self()->bufferSwapComplete();
        }
    }
}

void DrmBackend::openDrm()
{
    connect(LogindIntegration::self(), &LogindIntegration::sessionActiveChanged, this, &DrmBackend::activate);
    UdevDevice::Ptr device = m_udev->primaryGpu();
    if (!device) {
        qCWarning(KWIN_DRM) << "Did not find a GPU";
        return;
    }
    m_devNode = qEnvironmentVariableIsSet("KWIN_DRM_DEVICE_NODE") ? qgetenv("KWIN_DRM_DEVICE_NODE") : QByteArray(device->devNode());
    int fd = LogindIntegration::self()->takeDevice(m_devNode.constData());
    if (fd < 0) {
        qCWarning(KWIN_DRM) << "failed to open drm device at" << m_devNode;
        return;
    }
    m_fd = fd;
    m_active = true;
    QSocketNotifier *notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this,
        [this] {
            if (!LogindIntegration::self()->isActiveSession()) {
                return;
            }
            drmEventContext e;
            memset(&e, 0, sizeof e);
            e.version = KWIN_DRM_EVENT_CONTEXT_VERSION;

            if (m_atomicModeSetting) {
                e.page_flip_handler2 = atomicFlipHandler;
            } else {
                e.page_flip_handler = legacyFlipHandler;
            }

            drmHandleEvent(m_fd, &e);
        }
    );
    m_drmId = device->sysNum();

    // trying to activate Atomic Mode Setting (this means also Universal Planes)
    if (!qEnvironmentVariableIsSet("KWIN_DRM_NO_AMS")) {
        if (drmSetClientCap(m_fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0) {
            qCDebug(KWIN_DRM) << "Using Atomic Mode Setting.";
            m_atomicModeSetting = true;

            DrmScopedPointer<drmModePlaneRes> planeResources(drmModeGetPlaneResources(m_fd));
            if (!planeResources) {
                qCWarning(KWIN_DRM) << "Failed to get plane resources. Falling back to legacy mode";
                m_atomicModeSetting = false;
            }

            if (m_atomicModeSetting) {
                qCDebug(KWIN_DRM) << "Number of planes:" << planeResources->count_planes;

                // create the plane objects
                for (unsigned int i = 0; i < planeResources->count_planes; ++i) {
                    DrmScopedPointer<drmModePlane> kplane(drmModeGetPlane(m_fd, planeResources->planes[i]));
                    DrmPlane *p = new DrmPlane(kplane->plane_id, m_fd);
                    if (p->atomicInit()) {
                        m_planes << p;
                        if (p->type() == DrmPlane::TypeIndex::Overlay) {
                            m_overlayPlanes << p;
                        }
                    } else {
                        delete p;
                    }
                }

                if (m_planes.isEmpty()) {
                    qCWarning(KWIN_DRM) << "Failed to create any plane. Falling back to legacy mode";
                    m_atomicModeSetting = false;
                }
            }
        } else {
            qCWarning(KWIN_DRM) << "drmSetClientCap for Atomic Mode Setting failed. Using legacy mode.";
        }
    }

    DrmScopedPointer<drmModeRes> resources(drmModeGetResources(m_fd));
    if (!resources) {
        qCWarning(KWIN_DRM) << "drmModeGetResources failed";
        return;
    }

    for (int i = 0; i < resources->count_connectors; ++i) {
        m_connectors << new DrmConnector(resources->connectors[i], m_fd);
    }
    for (int i = 0; i < resources->count_crtcs; ++i) {
        m_crtcs << new DrmCrtc(resources->crtcs[i], this, i);
    }

    if (m_atomicModeSetting) {
        auto tryAtomicInit = [] (DrmObject *obj) -> bool {
            if (obj->atomicInit()) {
                return false;
            } else {
                delete obj;
                return true;
            }
        };
        m_connectors.erase(std::remove_if(m_connectors.begin(), m_connectors.end(), tryAtomicInit), m_connectors.end());
        m_crtcs.erase(std::remove_if(m_crtcs.begin(), m_crtcs.end(), tryAtomicInit), m_crtcs.end());
    }

    initCursor();
    updateOutputs();

    if (m_outputs.isEmpty()) {
        qCDebug(KWIN_DRM) << "No connected outputs found on startup.";
    }

    // setup udevMonitor
    if (m_udevMonitor) {
        m_udevMonitor->filterSubsystemDevType("drm");
        const int fd = m_udevMonitor->fd();
        if (fd != -1) {
            QSocketNotifier *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(notifier, &QSocketNotifier::activated, this,
                [this] {
                    auto device = m_udevMonitor->getDevice();
                    if (!device) {
                        return;
                    }
                    if (device->sysNum() != m_drmId) {
                        return;
                    }
                    if (device->hasProperty("HOTPLUG", "1")) {
                        qCDebug(KWIN_DRM) << "Received hot plug event for monitored drm device";
                        updateOutputs();
                        updateCursor();
                    }
                }
            );
            m_udevMonitor->enable();
        }
    }
    setReady(true);
}

/**
 * For some reason directly after VT-switch the crtc mode struct does not have all information the
 * modes in connector struct have. From runtime tests missing are vrefresh, type, name values, maybe
 * also hskew and vscan (both were 0 in runtime tests so can not say).
 *
 * To circumvent this issue and still avoid an unnecessary mode change in the beginning compare only
 * values that are known to be available and choose a matching mode then as the current one.
 *
 * @param crtc with the current mode
 * @param connector with the list of modes
 * @return
 */
drmModeModeInfo getInitialMode(drmModeCrtcPtr crtc, drmModeConnectorPtr connector)
{
    if (crtc->mode_valid) {
        const auto crtcMode = crtc->mode;

        qCDebug(KWIN_DRM) << "Current crtc mode:"
                          << "clock:"         << crtcMode.clock
                          << "hdisplay:"      << crtcMode.hdisplay
                          << "hsync_start:"   << crtcMode.hsync_start
                          << "hsync_end:"     << crtcMode.hsync_end
                          << "htotal:"        << crtcMode.htotal
                          << "hskew:"         << crtcMode.hskew << "\n"
                          << "vdisplay:"      << crtcMode.vdisplay
                          << "vsync_start:"   << crtcMode.vsync_start
                          << "vsync_end:"     << crtcMode.vsync_end
                          << "vtotal:"        << crtcMode.vtotal
                          << "vscan:"         << crtcMode.vscan << "\n"
                          << "vrefresh:"      << crtcMode.vrefresh
                          << "flags:"         << crtcMode.flags
                          << "type:"          << crtcMode.type
                          << "name:"          << crtcMode.name;

        for (int i = 0; i < connector->count_modes; i++) {
            const auto conMode = connector->modes[i];

            qCDebug(KWIN_DRM) << "Mode" << i << "in list:"
                              << "clock:"         << conMode.clock
                              << "hdisplay:"      << conMode.hdisplay
                              << "hsync_start:"   << conMode.hsync_start
                              << "hsync_end:"     << conMode.hsync_end
                              << "htotal:"        << conMode.htotal
                              << "hskew:"         << conMode.hskew << "\n"
                              << "vdisplay:"      << conMode.vdisplay
                              << "vsync_start:"   << conMode.vsync_start
                              << "vsync_end:"     << conMode.vsync_end
                              << "vtotal:"        << conMode.vtotal
                              << "vscan:"         << conMode.vscan << "\n"
                              << "vrefresh:"      << conMode.vrefresh
                              << "flags:"         << conMode.flags
                              << "type:"          << conMode.type
                              << "name:"          << conMode.name;

            if (       crtcMode.clock       == conMode.clock
                    && crtcMode.hdisplay    == conMode.hdisplay
                    && crtcMode.hsync_start == conMode.hsync_start
                    && crtcMode.hsync_end   == conMode.hsync_end
                    && crtcMode.htotal      == conMode.htotal
//                    && crtcMode.hskew      == conMode.hskew
                    && crtcMode.vdisplay    == conMode.vdisplay
                    && crtcMode.vsync_start == conMode.vsync_start
                    && crtcMode.vsync_end   == conMode.vsync_end
                    && crtcMode.vtotal      == conMode.vtotal
//                    && crtcMode.vscan       == conMode.vscan
//                    && crtcMode.vrefresh    == conMode.vrefresh
                    && crtcMode.flags       == conMode.flags
//                    && crtcMode.type        == conMode.type
//                    && crtcMode.name        == conMode.name
                       ) {
                    qCDebug(KWIN_DRM) << "Matching mode found in connector mode list.";
                    return conMode;
            }
        }
    }
    return connector->modes[0];
}

void DrmBackend::updateOutputs()
{
    if (m_fd < 0) {
        return;
    }

    DrmScopedPointer<drmModeRes> resources(drmModeGetResources(m_fd));
    if (!resources) {
        qCWarning(KWIN_DRM) << "drmModeGetResources failed";
        return;
    }

    QVector<DrmOutput*> connectedOutputs;
    QVector<DrmConnector*> pendingConnectors;

    // split up connected connectors in already or not yet assigned ones
    for (DrmConnector *con : qAsConst(m_connectors)) {
        if (!con->isConnected()) {
            continue;
        }

        if (DrmOutput *o = findOutput(con->id())) {
            connectedOutputs << o;
        } else {
            pendingConnectors << con;
        }
    }

    // check for outputs which got removed
    auto it = m_outputs.begin();
    while (it != m_outputs.end()) {
        if (connectedOutputs.contains(*it)) {
            it++;
            continue;
        }
        DrmOutput *removed = *it;
        it = m_outputs.erase(it);
        m_enabledOutputs.removeOne(removed);
        emit outputRemoved(removed);
        removed->teardown();
    }

    // now check new connections
    for (DrmConnector *con : qAsConst(pendingConnectors)) {
        DrmScopedPointer<drmModeConnector> connector(drmModeGetConnector(m_fd, con->id()));
        if (!connector) {
            continue;
        }
        if (connector->count_modes == 0) {
            continue;
        }
        bool outputDone = false;

        QVector<uint32_t> encoders = con->encoders();
        for (auto encId : qAsConst(encoders)) {
            DrmScopedPointer<drmModeEncoder> encoder(drmModeGetEncoder(m_fd, encId));
            if (!encoder) {
                continue;
            }
            for (DrmCrtc *crtc : qAsConst(m_crtcs)) {
                if (!(encoder->possible_crtcs & (1 << crtc->resIndex()))) {
                        continue;
                }

                // check if crtc isn't used yet -- currently we don't allow multiple outputs on one crtc (cloned mode)
                auto it = std::find_if(connectedOutputs.constBegin(), connectedOutputs.constEnd(),
                    [crtc] (DrmOutput *o) {
                        return o->m_crtc == crtc;
                    }
                );
                if (it != connectedOutputs.constEnd()) {
                    continue;
                }

                // we found a suitable encoder+crtc
                // TODO: we could avoid these lib drm calls if we store all struct data in DrmCrtc and DrmConnector in the beginning
                DrmScopedPointer<drmModeCrtc> modeCrtc(drmModeGetCrtc(m_fd, crtc->id()));
                if (!modeCrtc) {
                    continue;
                }

                DrmOutput *output = new DrmOutput(this);
                con->setOutput(output);
                output->m_conn = con;
                crtc->setOutput(output);
                output->m_crtc = crtc;

                output->m_mode = getInitialMode(modeCrtc.get(), connector.get());
                qCDebug(KWIN_DRM) << "For new output use mode " << output->m_mode.name;

                if (!output->init(connector.data())) {
                    qCWarning(KWIN_DRM) << "Failed to create output for connector " << con->id();
                    delete output;
                    continue;
                }
                if (!output->initCursor(m_cursorSize)) {
                    setSoftWareCursor(true);
                }
                qCDebug(KWIN_DRM) << "Found new output with uuid" << output->uuid();

                connectedOutputs << output;
                emit outputAdded(output);
                outputDone = true;
                break;
            }
            if (outputDone) {
                break;
            }
        }
    }
    std::sort(connectedOutputs.begin(), connectedOutputs.end(), [] (DrmOutput *a, DrmOutput *b) { return a->m_conn->id() < b->m_conn->id(); });
    m_outputs = connectedOutputs;
    m_enabledOutputs = connectedOutputs;
    readOutputsConfiguration();
    updateOutputsEnabled();
    if (!m_outputs.isEmpty()) {
        emit screensQueried();
    }
}

void DrmBackend::readOutputsConfiguration()
{
    if (m_outputs.isEmpty()) {
        return;
    }
    const QByteArray uuid = generateOutputConfigurationUuid();
    const auto outputGroup = kwinApp()->config()->group("DrmOutputs");
    const auto configGroup = outputGroup.group(uuid);

    // Default position goes from left to right.
    double width = 0;
    for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
        qCDebug(KWIN_DRM) << "Reading output configuration for [" << uuid << "] ["<< (*it)->uuid() << "]";

        const auto outputConfig = configGroup.group((*it)->uuid());
        const QRectF geo = outputConfig.readEntry<QRectF>("Geometry",
                                QRectF(width, 0, (*it)->m_mode.hdisplay, (*it)->m_mode.vdisplay));
        (*it)->forceGeometry(geo);

        width += (*it)->geometry().width();
    }
}

void DrmBackend::writeOutputsConfiguration()
{
    if (m_outputs.isEmpty()) {
        return;
    }
    const QByteArray uuid = generateOutputConfigurationUuid();
    auto configGroup = KSharedConfig::openConfig()->group("DrmOutputs").group(uuid);
    // default position goes from left to right
    for (auto it = m_outputs.cbegin(); it != m_outputs.cend(); ++it) {
        qCDebug(KWIN_DRM) << "Writing output configuration for [" << uuid << "] ["<< (*it)->uuid() << "]";
        auto outputConfig = configGroup.group((*it)->uuid());
        outputConfig.writeEntry("Geometry", QRectF((*it)->geometry()));
    }
}

QByteArray DrmBackend::generateOutputConfigurationUuid() const
{
    auto it = m_outputs.constBegin();
    if (m_outputs.size() == 1) {
        // special case: one output
        return (*it)->uuid();
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (; it != m_outputs.constEnd(); ++it) {
        hash.addData((*it)->uuid());
    }
    return hash.result().toHex().left(10);
}

void DrmBackend::enableOutput(DrmOutput *output, bool enable)
{
    if (enable) {
        Q_ASSERT(!m_enabledOutputs.contains(output));
        m_enabledOutputs << output;
        emit outputAdded(output);
    } else {
        Q_ASSERT(m_enabledOutputs.contains(output));
        m_enabledOutputs.removeOne(output);
        Q_ASSERT(!m_enabledOutputs.contains(output));
        emit outputRemoved(output);
    }
    updateOutputsEnabled();
    checkOutputsAreOn();
    emit screensQueried();
}

DrmOutput *DrmBackend::findOutput(quint32 connector)
{
    auto it = std::find_if(m_outputs.constBegin(), m_outputs.constEnd(), [connector] (DrmOutput *o) {
        return o->m_conn->id() == connector;
    });
    if (it != m_outputs.constEnd()) {
        return *it;
    }
    return nullptr;
}

bool DrmBackend::present(DrmBuffer *buffer, DrmOutput *output)
{
    if (!buffer || buffer->bufferId() == 0) {
        if (m_deleteBufferAfterPageFlip) {
            delete buffer;
        }
        return false;
    }

    if (output->present(buffer)) {
        m_pageFlipsPending++;
        if (m_pageFlipsPending == 1 && Compositor::self()) {
            Compositor::self()->aboutToSwapBuffers();
        }
        return true;
    } else if (m_deleteBufferAfterPageFlip) {
        delete buffer;
    }
    return false;
}

void DrmBackend::initCursor()
{

#if HAVE_EGL_STREAMS
    // Hardware cursors aren't currently supported with EGLStream backend,
    // possibly an NVIDIA driver bug
    if (m_useEglStreams) {
        setSoftWareCursor(true);
    }
#endif

    m_cursorEnabled = waylandServer()->seat()->hasPointer();
    connect(waylandServer()->seat(), &Wrapland::Server::Seat::hasPointerChanged, this,
        [this] {
            m_cursorEnabled = waylandServer()->seat()->hasPointer();
            if (usesSoftwareCursor()) {
                return;
            }
            for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
                if (m_cursorEnabled) {
                    if (!(*it)->showCursor()) {
                        setSoftWareCursor(true);
                    }
                } else {
                    (*it)->hideCursor();
                }
            }
        }
    );
    uint64_t capability = 0;
    QSize cursorSize;
    if (drmGetCap(m_fd, DRM_CAP_CURSOR_WIDTH, &capability) == 0) {
        cursorSize.setWidth(capability);
    } else {
        cursorSize.setWidth(64);
    }
    if (drmGetCap(m_fd, DRM_CAP_CURSOR_HEIGHT, &capability) == 0) {
        cursorSize.setHeight(capability);
    } else {
        cursorSize.setHeight(64);
    }
    m_cursorSize = cursorSize;
    // now we have screens and can set cursors, so start tracking
    connect(this, &DrmBackend::cursorChanged, this, &DrmBackend::updateCursor);
    connect(Cursor::self(), &Cursor::posChanged, this, &DrmBackend::moveCursor);
}

void DrmBackend::setCursor()
{
    if (m_cursorEnabled) {
        for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
            if (!(*it)->showCursor()) {
                setSoftWareCursor(true);
            }
        }
    }
    markCursorAsRendered();
}

void DrmBackend::updateCursor()
{
    if (usesSoftwareCursor()) {
        return;
    }
    if (isCursorHidden()) {
        return;
    }
    const QImage &cursorImage = softwareCursor();
    if (cursorImage.isNull()) {
        doHideCursor();
        return;
    }
    for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
        (*it)->updateCursor();
    }

    setCursor();
    moveCursor();
}

void DrmBackend::doShowCursor()
{
    updateCursor();
}

void DrmBackend::doHideCursor()
{
    if (!m_cursorEnabled || usesSoftwareCursor()) {
        return;
    }
    for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
        (*it)->hideCursor();
    }
}

void DrmBackend::moveCursor()
{
    if (!m_cursorEnabled || isCursorHidden() || usesSoftwareCursor()) {
        return;
    }
    for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
        (*it)->moveCursor(Cursor::pos());
    }
}

Screens *DrmBackend::createScreens(QObject *parent)
{
    return new DrmScreens(this, parent);
}

QPainterBackend *DrmBackend::createQPainterBackend()
{
    m_deleteBufferAfterPageFlip = false;
    return new DrmQPainterBackend(this);
}

OpenGLBackend *DrmBackend::createOpenGLBackend()
{
#if HAVE_EGL_STREAMS
    if (m_useEglStreams) {
        m_deleteBufferAfterPageFlip = false;
        return new EglStreamBackend(this);
    }
#endif

#if HAVE_GBM
    m_deleteBufferAfterPageFlip = true;
    return new EglGbmBackend(this);
#else
    return Platform::createOpenGLBackend();
#endif
}

DrmDumbBuffer *DrmBackend::createBuffer(const QSize &size)
{
    DrmDumbBuffer *b = new DrmDumbBuffer(m_fd, size);
    return b;
}

#if HAVE_GBM
DrmSurfaceBuffer *DrmBackend::createBuffer(const std::shared_ptr<GbmSurface> &surface)
{
    DrmSurfaceBuffer *b = new DrmSurfaceBuffer(m_fd, surface);
    return b;
}
#endif

void DrmBackend::updateOutputsEnabled()
{
    bool enabled = false;
    for (auto it = m_enabledOutputs.constBegin(); it != m_enabledOutputs.constEnd(); ++it) {
        enabled = enabled || (*it)->isDpmsEnabled();
    }
    setOutputsEnabled(enabled);
}

QVector<CompositingType> DrmBackend::supportedCompositors() const
{
    if (selectedCompositor() != NoCompositing) {
        return {selectedCompositor()};
    }
#if HAVE_GBM
    return QVector<CompositingType>{OpenGLCompositing, QPainterCompositing};
#elif HAVE_EGL_STREAMS
    return m_useEglStreams ?
        QVector<CompositingType>{OpenGLCompositing, QPainterCompositing} :
        QVector<CompositingType>{QPainterCompositing};
#else
    return QVector<CompositingType>{QPainterCompositing};
#endif
}

QString DrmBackend::supportInformation() const
{
    QString supportInfo;
    QDebug s(&supportInfo);
    s.nospace();
    s << "Name: " << "DRM" << endl;
    s << "Active: " << m_active << endl;
    s << "Atomic Mode Setting: " << m_atomicModeSetting << endl;
#if HAVE_EGL_STREAMS
    s << "Using EGL Streams: " << m_useEglStreams << endl;
#endif
    return supportInfo;
}

}
