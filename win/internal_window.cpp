/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Martin Flöser <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "internal_window.h"

#include "control.h"
#include "deco/bridge.h"
#include "deco/window.h"
#include "desktop_set.h"
#include "fullscreen.h"
#include "geo.h"
#include "maximize.h"
#include "meta.h"
#include "remnant.h"
#include "render/wayland/buffer.h"
#include "scene.h"
#include "setup.h"
#include "space.h"
#include "space_areas_helpers.h"
#include "window_release.h"

#include <KDecoration2/Decoration>

#include <QOpenGLFramebufferObject>
#include <QWindow>

Q_DECLARE_METATYPE(NET::WindowType)

static const QByteArray s_skipClosePropertyName = QByteArrayLiteral("KWIN_SKIP_CLOSE_ANIMATION");

namespace KWin::win
{

class internal_control : public control
{
public:
    internal_control(internal_window* client)
        : control(client)
        , m_client{client}
    {
    }

    void set_desktops(QVector<virtual_desktop*> /*desktops*/) override
    {
    }

    void destroy_decoration() override
    {
        if (!win::decoration(m_client)) {
            return;
        }

        auto const client_geo = win::frame_to_client_rect(m_client, m_client->frameGeometry());
        control::destroy_decoration();
        m_client->setFrameGeometry(client_geo);
    }

private:
    internal_window* m_client;
};

internal_window::internal_window(win::remnant remnant, win::space& space)
    : Toplevel(std::move(remnant), space)
{
}

internal_window::internal_window(QWindow* window, win::space& space)
    : Toplevel(space)
    , m_internalWindow(window)
    , synced_geo(window->geometry())
    , m_internalWindowFlags(window->flags())
{
    control = std::make_unique<internal_control>(this);

    connect(
        m_internalWindow, &QWindow::xChanged, this, &internal_window::updateInternalWindowGeometry);
    connect(
        m_internalWindow, &QWindow::yChanged, this, &internal_window::updateInternalWindowGeometry);
    connect(m_internalWindow,
            &QWindow::widthChanged,
            this,
            &internal_window::updateInternalWindowGeometry);
    connect(m_internalWindow,
            &QWindow::heightChanged,
            this,
            &internal_window::updateInternalWindowGeometry);
    connect(m_internalWindow, &QWindow::windowTitleChanged, this, &internal_window::setCaption);
    connect(m_internalWindow, &QWindow::opacityChanged, this, &internal_window::setOpacity);
    connect(m_internalWindow, &QWindow::destroyed, this, &internal_window::destroyClient);

    connect(this, &internal_window::opacityChanged, this, &internal_window::addRepaintFull);

    const QVariant windowType = m_internalWindow->property("kwin_windowType");
    if (!windowType.isNull()) {
        m_windowType = windowType.value<NET::WindowType>();
    }

    setCaption(m_internalWindow->title());
    control->set_icon(QIcon::fromTheme(QStringLiteral("kwin")));
    win::set_on_all_desktops(this, true);
    setOpacity(m_internalWindow->opacity());
    setSkipCloseAnimation(m_internalWindow->property(s_skipClosePropertyName).toBool());

    setupCompositing();
    updateColorScheme();

    win::block_geometry_updates(this, true);
    updateDecoration(true);
    setFrameGeometry(win::client_to_frame_rect(this, m_internalWindow->geometry()));
    restore_geometries.maximize = frameGeometry();
    win::block_geometry_updates(this, false);

    m_internalWindow->installEventFilter(this);
}

internal_window::~internal_window() = default;

bool internal_window::setupCompositing()
{
    return win::setup_compositing(*this, false);
}

void internal_window::add_scene_window_addon()
{
    auto setup_buffer = [this](auto& buffer) {
        auto win_integrate = std::make_unique<render::wayland::buffer_win_integration>(buffer);
        auto update_helper = [&buffer]() {
            auto win = static_cast<internal_window*>(buffer.toplevel());
            auto& win_integrate
                = static_cast<render::wayland::buffer_win_integration&>(*buffer.win_integration);
            if (win->buffers.fbo) {
                win_integrate.internal.fbo = win->buffers.fbo;
                return;
            }
            if (!win->buffers.image.isNull()) {
                win_integrate.internal.image = win->buffers.image;
            }
        };
        win_integrate->update = update_helper;
        buffer.win_integration = std::move(win_integrate);
    };

    render->win_integration.setup_buffer = setup_buffer;
}

bool internal_window::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_internalWindow && event->type() == QEvent::DynamicPropertyChange) {
        QDynamicPropertyChangeEvent* pe = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (pe->propertyName() == s_skipClosePropertyName) {
            setSkipCloseAnimation(m_internalWindow->property(s_skipClosePropertyName).toBool());
        }
        if (pe->propertyName() == "kwin_windowType") {
            m_windowType = m_internalWindow->property("kwin_windowType").value<NET::WindowType>();
            update_space_areas(space);
        }
    }
    return false;
}

qreal internal_window::bufferScale() const
{
    return remnant ? remnant->data.buffer_scale : buffer_scale_internal();
}

void internal_window::debug(QDebug& stream) const
{
    if (remnant) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
        return;
    }
    stream.nospace() << "\'internal_window:" << m_internalWindow << "\'";
}

NET::WindowType internal_window::windowType(bool direct, int supported_types) const
{
    Q_UNUSED(direct)
    Q_UNUSED(supported_types)
    return remnant ? remnant->data.window_type : m_windowType;
}

double internal_window::opacity() const
{
    return remnant ? remnant->data.opacity : m_opacity;
}

void internal_window::setOpacity(double opacity)
{
    if (m_opacity == opacity) {
        return;
    }

    const double oldOpacity = m_opacity;
    m_opacity = opacity;

    Q_EMIT opacityChanged(this, oldOpacity);
}

void internal_window::killWindow()
{
    // We don't kill our internal windows.
}

bool internal_window::is_popup_end() const
{
    return remnant ? remnant->data.was_popup_window : m_internalWindowFlags.testFlag(Qt::Popup);
}

QByteArray internal_window::windowRole() const
{
    return QByteArray();
}

void internal_window::closeWindow()
{
    if (m_internalWindow) {
        m_internalWindow->hide();
    }
}

bool internal_window::isCloseable() const
{
    return true;
}

bool internal_window::isMaximizable() const
{
    return false;
}

bool internal_window::isMinimizable() const
{
    return false;
}

bool internal_window::isMovable() const
{
    return true;
}

bool internal_window::isMovableAcrossScreens() const
{
    return true;
}

bool internal_window::isResizable() const
{
    return true;
}

bool internal_window::placeable() const
{
    return !m_internalWindowFlags.testFlag(Qt::BypassWindowManagerHint)
        && !m_internalWindowFlags.testFlag(Qt::Popup);
}

bool internal_window::noBorder() const
{
    if (remnant) {
        return remnant->data.no_border;
    }
    return m_userNoBorder || m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
        || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool internal_window::userCanSetNoBorder() const
{
    return !m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
        || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool internal_window::wantsInput() const
{
    return false;
}

bool internal_window::isInternal() const
{
    return true;
}

bool internal_window::isLockScreen() const
{
    if (m_internalWindow) {
        return m_internalWindow->property("org_kde_ksld_emergency").toBool();
    }
    return false;
}

bool internal_window::isOutline() const
{
    if (remnant) {
        return remnant->data.was_outline;
    }
    if (m_internalWindow) {
        return m_internalWindow->property("__kwin_outline").toBool();
    }
    return false;
}

bool internal_window::isShown() const
{
    return ready_for_painting;
}

bool internal_window::isHiddenInternal() const
{
    return false;
}

void internal_window::hideClient(bool hide)
{
    Q_UNUSED(hide)
}

void internal_window::setFrameGeometry(QRect const& rect)
{
    geometry_update.frame = rect;

    if (geometry_update.block) {
        geometry_update.pending = win::pending_geometry::normal;
        return;
    }

    geometry_update.pending = win::pending_geometry::none;

    if (synced_geo != win::frame_to_client_rect(this, rect)) {
        requestGeometry(rect);
        return;
    }

    do_set_geometry(rect);
}

void internal_window::apply_restore_geometry(QRect const& restore_geo)
{
    setFrameGeometry(rectify_restore_geometry(this, restore_geo));
}

void internal_window::restore_geometry_from_fullscreen()
{
}

void internal_window::do_set_geometry(QRect const& frame_geo)
{
    auto const old_frame_geo = frameGeometry();

    if (old_frame_geo == frame_geo) {
        return;
    }

    set_frame_geometry(frame_geo);

    if (win::is_resize(this)) {
        win::perform_move_resize(this);
    }

    space.render.addRepaint(visible_rect(this));

    Q_EMIT frame_geometry_changed(this, old_frame_geo);
}

bool internal_window::hasStrut() const
{
    return false;
}

bool internal_window::supportsWindowRules() const
{
    return false;
}

void internal_window::takeFocus()
{
}

bool internal_window::userCanSetFullScreen() const
{
    return false;
}

void internal_window::setFullScreen(bool set, bool user)
{
    Q_UNUSED(set)
    Q_UNUSED(user)
}

void internal_window::setNoBorder(bool set)
{
    if (!userCanSetNoBorder()) {
        return;
    }
    if (m_userNoBorder == set) {
        return;
    }
    m_userNoBorder = set;
    updateDecoration(true);
}

void internal_window::updateDecoration(bool check_workspace_pos, bool force)
{
    if (!force && (win::decoration(this) != nullptr) == !noBorder()) {
        return;
    }

    const QRect oldFrameGeometry = frameGeometry();
    const QRect oldClientGeometry = oldFrameGeometry - win::frame_margins(this);

    win::geometry_updates_blocker blocker(this);

    if (force) {
        control->destroy_decoration();
    }

    if (!noBorder()) {
        createDecoration(oldClientGeometry);
    } else {
        control->destroy_decoration();
    }

    win::update_shadow(this);

    if (check_workspace_pos) {
        win::check_workspace_position(this, oldFrameGeometry, -2, oldClientGeometry);
    }
}

void internal_window::updateColorScheme()
{
    win::set_color_scheme(this, QString());
}

void internal_window::showOnScreenEdge()
{
}

void internal_window::checkTransient(Toplevel* /*window*/)
{
}

bool internal_window::belongsToDesktop() const
{
    return false;
}

void internal_window::destroyClient()
{
    if (control->move_resize().enabled) {
        leaveMoveResize();
    }

    auto deleted = create_remnant_window<internal_window>(*this);
    Q_EMIT closed(this);

    control->destroy_decoration();

    remove_window_from_lists(space, this);
    space.stacking_order->update_count();
    update_space_areas(space);
    Q_EMIT space.qobject->internalClientRemoved(this);

    m_internalWindow = nullptr;

    if (deleted) {
        deleted->remnant->unref();
        delete this;
    } else {
        delete_window_from_space(space, this);
    }
}

void internal_window::present(std::shared_ptr<QOpenGLFramebufferObject> const& fbo)
{
    assert(buffers.image.isNull());

    const QSize bufferSize = fbo->size() / buffer_scale_internal();

    setFrameGeometry(QRect(pos(), win::client_to_frame_size(this, bufferSize)));
    markAsMapped();

    if (buffers.fbo != fbo) {
        discard_buffer();
        buffers.fbo = fbo;
    }

    setDepth(32);
    addDamageFull();
    addRepaintFull();
}

void internal_window::present(const QImage& image, const QRegion& damage)
{
    assert(!buffers.fbo);

    const QSize bufferSize = image.size() / buffer_scale_internal();

    setFrameGeometry(QRect(pos(), win::client_to_frame_size(this, bufferSize)));
    markAsMapped();

    if (buffers.image.size() != image.size()) {
        discard_buffer();
    }

    buffers.image = image;

    setDepth(32);
    addDamage(damage);
}

QWindow* internal_window::internalWindow() const
{
    return m_internalWindow;
}

bool internal_window::acceptsFocus() const
{
    return false;
}

bool internal_window::belongsToSameApplication(Toplevel const* other,
                                               [[maybe_unused]] win::same_client_check checks) const
{
    return qobject_cast<internal_window const*>(other) != nullptr;
}

bool internal_window::has_pending_repaints() const
{
    return isShown() && Toplevel::has_pending_repaints();
}

void internal_window::doResizeSync()
{
    requestGeometry(control->move_resize().geometry);
}

void internal_window::updateCaption()
{
    auto const oldSuffix = caption.suffix;
    const auto shortcut = win::shortcut_caption_suffix(this);
    caption.suffix = shortcut;
    if ((!win::is_special_window(this) || win::is_toolbar(this))
        && win::find_client_with_same_caption(static_cast<Toplevel*>(this))) {
        int i = 2;
        do {
            caption.suffix = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
            i++;
        } while (win::find_client_with_same_caption(static_cast<Toplevel*>(this)));
    }
    if (caption.suffix != oldSuffix) {
        Q_EMIT captionChanged();
    }
}

double internal_window::buffer_scale_internal() const
{
    if (m_internalWindow) {
        return m_internalWindow->devicePixelRatio();
    }
    return 1;
}

void internal_window::createDecoration(const QRect& rect)
{
    control->deco().window = new deco::window(this);
    auto decoration = space.deco->createDecoration(control->deco().window);

    if (decoration) {
        QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);
        connect(decoration, &KDecoration2::Decoration::shadowChanged, this, [this] {
            win::update_shadow(this);
        });
        connect(decoration, &KDecoration2::Decoration::bordersChanged, this, [this]() {
            win::geometry_updates_blocker blocker(this);
            auto const old_geo = frameGeometry();
            win::check_workspace_position(this, old_geo);
            discard_quads();
            control->deco().client->update_size();
        });
    }

    control->deco().decoration = decoration;
    setFrameGeometry(win::client_to_frame_rect(this, rect));
    discard_quads();
}

void internal_window::requestGeometry(const QRect& rect)
{
    if (m_internalWindow) {
        m_internalWindow->setGeometry(win::frame_to_client_rect(this, rect));
        synced_geo = rect;
    }
}

void internal_window::setCaption(QString const& cap)
{
    if (caption.normal == cap) {
        return;
    }

    caption.normal = cap;

    auto const oldCaptionSuffix = caption.suffix;
    updateCaption();

    if (caption.suffix == oldCaptionSuffix) {
        Q_EMIT captionChanged();
    }
}

void internal_window::markAsMapped()
{
    if (ready_for_painting) {
        return;
    }

    setReadyForPainting();

    space.windows.push_back(this);

    setup_space_window_connections(&space, this);
    update_layer(this);

    if (placeable()) {
        auto const area
            = space_window_area(space, PlacementArea, get_current_output(space), desktop());
        place(this, area);
    }

    space.stacking_order->update_count();
    update_space_areas(space);

    Q_EMIT space.qobject->internalClientAdded(this);
}

void internal_window::updateInternalWindowGeometry()
{
    if (control->move_resize().enabled) {
        return;
    }
    if (!m_internalWindow) {
        // Might be called in dtor of QWindow
        // TODO: Can this be ruled out through other means?
        return;
    }

    do_set_geometry(win::client_to_frame_rect(this, m_internalWindow->geometry()));
}

}
