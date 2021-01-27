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
#include "internal_client.h"
#include "decorations/decorationbridge.h"
#include "decorations/window.h"
#include "workspace.h"

#include "win/control.h"
#include "win/geo.h"
#include "win/meta.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/setup.h"

#include <KDecoration2/Decoration>

#include <QOpenGLFramebufferObject>
#include <QWindow>

Q_DECLARE_METATYPE(NET::WindowType)

static const QByteArray s_skipClosePropertyName = QByteArrayLiteral("KWIN_SKIP_CLOSE_ANIMATION");

namespace KWin
{

class internal_control : public win::control
{
public:
    internal_control(InternalClient* client)
        : control(client)
        , m_client{client}
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
    InternalClient* m_client;
};

InternalClient::InternalClient(QWindow *window)
    : m_internalWindow(window)
    , synced_geo(window->geometry())
    , m_windowId(window->winId())
    , m_internalWindowFlags(window->flags())
{
    control = std::make_unique<internal_control>(this);

    connect(m_internalWindow, &QWindow::xChanged, this, &InternalClient::updateInternalWindowGeometry);
    connect(m_internalWindow, &QWindow::yChanged, this, &InternalClient::updateInternalWindowGeometry);
    connect(m_internalWindow, &QWindow::widthChanged, this, &InternalClient::updateInternalWindowGeometry);
    connect(m_internalWindow, &QWindow::heightChanged, this, &InternalClient::updateInternalWindowGeometry);
    connect(m_internalWindow, &QWindow::windowTitleChanged, this, &InternalClient::setCaption);
    connect(m_internalWindow, &QWindow::opacityChanged, this, &InternalClient::setOpacity);
    connect(m_internalWindow, &QWindow::destroyed, this, &InternalClient::destroyClient);

    connect(this, &InternalClient::opacityChanged, this, &InternalClient::addRepaintFull);

    const QVariant windowType = m_internalWindow->property("kwin_windowType");
    if (!windowType.isNull()) {
        m_windowType = windowType.value<NET::WindowType>();
    }

    setCaption(m_internalWindow->title());
    control->set_icon(QIcon::fromTheme(QStringLiteral("kwin")));
    win::set_on_all_desktops(this, true);
    setOpacity(m_internalWindow->opacity());
    setSkipCloseAnimation(m_internalWindow->property(s_skipClosePropertyName).toBool());

    setupCompositing(false);
    updateColorScheme();

    win::block_geometry_updates(this, true);
    updateDecoration(true);
    setFrameGeometry(win::client_to_frame_rect(this, m_internalWindow->geometry()));
    restore_geometries.maximize = frameGeometry();
    win::block_geometry_updates(this, false);

    m_internalWindow->installEventFilter(this);
}

InternalClient::~InternalClient()
{
}

bool InternalClient::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_internalWindow && event->type() == QEvent::DynamicPropertyChange) {
        QDynamicPropertyChangeEvent *pe = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (pe->propertyName() == s_skipClosePropertyName) {
            setSkipCloseAnimation(m_internalWindow->property(s_skipClosePropertyName).toBool());
        }
        if (pe->propertyName() == "kwin_windowType") {
            m_windowType = m_internalWindow->property("kwin_windowType").value<NET::WindowType>();
            workspace()->updateClientArea();
        }
    }
    return false;
}

QStringList InternalClient::activities() const
{
    return QStringList();
}

void InternalClient::blockActivityUpdates(bool b)
{
    Q_UNUSED(b)

    // Internal clients do not support activities.
}

qreal InternalClient::bufferScale() const
{
    if (m_internalWindow) {
        return m_internalWindow->devicePixelRatio();
    }
    return 1;
}

void InternalClient::debug(QDebug &stream) const
{
    stream.nospace() << "\'InternalClient:" << m_internalWindow << "\'";
}

NET::WindowType InternalClient::windowType(bool direct, int supported_types) const
{
    Q_UNUSED(direct)
    Q_UNUSED(supported_types)
    return m_windowType;
}

double InternalClient::opacity() const
{
    return m_opacity;
}

void InternalClient::setOpacity(double opacity)
{
    if (m_opacity == opacity) {
        return;
    }

    const double oldOpacity = m_opacity;
    m_opacity = opacity;

    emit opacityChanged(this, oldOpacity);
}

void InternalClient::killWindow()
{
    // We don't kill our internal windows.
}

bool InternalClient::is_popup_end() const
{
    return m_internalWindowFlags.testFlag(Qt::Popup);
}

QByteArray InternalClient::windowRole() const
{
    return QByteArray();
}

void InternalClient::closeWindow()
{
    if (m_internalWindow) {
        m_internalWindow->hide();
    }
}

bool InternalClient::isCloseable() const
{
    return true;
}

bool InternalClient::isMaximizable() const
{
    return false;
}

bool InternalClient::isMinimizable() const
{
    return false;
}

bool InternalClient::isMovable() const
{
    return true;
}

bool InternalClient::isMovableAcrossScreens() const
{
    return true;
}

bool InternalClient::isResizable() const
{
    return true;
}

bool InternalClient::noBorder() const
{
    return m_userNoBorder || m_internalWindowFlags.testFlag(Qt::FramelessWindowHint) || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool InternalClient::userCanSetNoBorder() const
{
    return !m_internalWindowFlags.testFlag(Qt::FramelessWindowHint) || m_internalWindowFlags.testFlag(Qt::Popup);
}

bool InternalClient::wantsInput() const
{
    return false;
}

bool InternalClient::isInternal() const
{
    return true;
}

bool InternalClient::isLockScreen() const
{
    if (m_internalWindow) {
        return m_internalWindow->property("org_kde_ksld_emergency").toBool();
    }
    return false;
}

bool InternalClient::isInputMethod() const
{
    if (m_internalWindow) {
        return m_internalWindow->property("__kwin_input_method").toBool();
    }
    return false;
}

bool InternalClient::isOutline() const
{
    if (m_internalWindow) {
        return m_internalWindow->property("__kwin_outline").toBool();
    }
    return false;
}

quint32 InternalClient::windowId() const
{
    return m_windowId;
}

bool InternalClient::isShown(bool shaded_is_shown) const
{
    Q_UNUSED(shaded_is_shown)

    return readyForPainting();
}

bool InternalClient::isHiddenInternal() const
{
    return false;
}

void InternalClient::hideClient(bool hide)
{
    Q_UNUSED(hide)
}

void InternalClient::setFrameGeometry(QRect const& rect)
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

void InternalClient::do_set_geometry(QRect const& frame_geo)
{
    auto const old_frame_geo = frameGeometry();

    if (old_frame_geo == frame_geo) {
        return;
    }

    set_frame_geometry(frame_geo);

    if (win::is_resize(this)) {
        win::perform_move_resize(this);
    }

    addWorkspaceRepaint(win::visible_rect(this));

    Q_EMIT frame_geometry_changed(this, old_frame_geo);
}

bool InternalClient::supportsWindowRules() const
{
    return false;
}

void InternalClient::setOnAllActivities(bool set)
{
    Q_UNUSED(set)

    // Internal clients do not support activities.
}

void InternalClient::takeFocus()
{
}

bool InternalClient::userCanSetFullScreen() const
{
    return false;
}

void InternalClient::setFullScreen(bool set, bool user)
{
    Q_UNUSED(set)
    Q_UNUSED(user)
}

void InternalClient::setNoBorder(bool set)
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

void InternalClient::updateDecoration(bool check_workspace_pos, bool force)
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

void InternalClient::updateColorScheme()
{
    win::set_color_scheme(this, QString());
}

void InternalClient::showOnScreenEdge()
{
}

void InternalClient::destroyClient()
{
    if (control->move_resize().enabled) {
        leaveMoveResize();
    }

    auto deleted = create_remnant(this);
    emit windowClosed(this, deleted);

    control->destroy_decoration();

    workspace()->removeInternalClient(this);

    deleted->remnant()->unref();
    m_internalWindow = nullptr;

    delete this;
}

void InternalClient::present(const QSharedPointer<QOpenGLFramebufferObject> fbo)
{
    Q_ASSERT(m_internalImage.isNull());

    const QSize bufferSize = fbo->size() / bufferScale();

    setFrameGeometry(QRect(pos(), win::client_to_frame_size(this, bufferSize)));
    markAsMapped();

    if (m_internalFBO != fbo) {
        discardWindowPixmap();
        m_internalFBO = fbo;
    }

    setDepth(32);
    addDamageFull();
    addRepaintFull();
}

void InternalClient::present(const QImage &image, const QRegion &damage)
{
    Q_ASSERT(m_internalFBO.isNull());

    const QSize bufferSize = image.size() / bufferScale();

    setFrameGeometry(QRect(pos(), win::client_to_frame_size(this, bufferSize)));
    markAsMapped();

    if (m_internalImage.size() != image.size()) {
        discardWindowPixmap();
    }

    m_internalImage = image;

    setDepth(32);
    addDamage(damage);
}

QWindow *InternalClient::internalWindow() const
{
    return m_internalWindow;
}

bool InternalClient::acceptsFocus() const
{
    return false;
}

bool InternalClient::belongsToSameApplication(Toplevel const* other,
                                              [[maybe_unused]] win::same_client_check checks) const
{
    return qobject_cast<InternalClient const*>(other) != nullptr;
}

void InternalClient::changeMaximize(bool horizontal, bool vertical, bool adjust)
{
    Q_UNUSED(horizontal)
    Q_UNUSED(vertical)
    Q_UNUSED(adjust)

    // Internal clients are not maximizable.
}

bool InternalClient::has_pending_repaints() const
{
    return isShown(true) && Toplevel::has_pending_repaints();
}

void InternalClient::doResizeSync()
{
    requestGeometry(control->move_resize().geometry);
}

void InternalClient::updateCaption()
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
        emit captionChanged();
    }
}

void InternalClient::createDecoration(const QRect &rect)
{
    control->deco().window = new Decoration::window(this);
    auto decoration = Decoration::DecorationBridge::self()->createDecoration(control->deco().window);

    if (decoration) {
        QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);
        connect(decoration, &KDecoration2::Decoration::shadowChanged,
                this, [this] { win::update_shadow(this); });
        connect(decoration, &KDecoration2::Decoration::bordersChanged, this,
            [this]() {
                win::geometry_updates_blocker blocker(this);
                const QRect oldGeometry = frameGeometry();
                if (!win::shaded(this)) {
                    win::check_workspace_position(this, oldGeometry);
                }
                discard_quads();
                control->deco().client->update_size();
            }
        );
    }

    control->deco().decoration = decoration;
    setFrameGeometry(win::client_to_frame_rect(this, rect));
    discard_quads();
}

void InternalClient::requestGeometry(const QRect &rect)
{
    if (m_internalWindow) {
        m_internalWindow->setGeometry(win::frame_to_client_rect(this, rect));
        synced_geo = rect;
    }
}

void InternalClient::setCaption(QString const& cap)
{
    if (caption.normal == cap) {
        return;
    }

    caption.normal = cap;

    auto const oldCaptionSuffix = caption.suffix;
    updateCaption();

    if (caption.suffix == oldCaptionSuffix) {
        emit captionChanged();
    }
}

void InternalClient::markAsMapped()
{
    if (!ready_for_painting) {
        setReadyForPainting();
        workspace()->addInternalClient(this);
    }
}

void InternalClient::updateInternalWindowGeometry()
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
