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

        auto const clientGeometry = win::frame_rect_to_client_rect(m_client,
                                                                   m_client->frameGeometry());
        control::destroy_decoration();
        m_client->setFrameGeometry(clientGeometry);
    }

    void do_move() override
    {
        m_client->syncGeometryToInternalWindow();
    }

private:
    InternalClient* m_client;
};

InternalClient::InternalClient(QWindow *window)
    : m_control{std::make_unique<internal_control>(this)}
    , m_internalWindow(window)
    , m_clientSize(window->size())
    , m_windowId(window->winId())
    , m_internalWindowFlags(window->flags())
{
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
    control()->set_icon(QIcon::fromTheme(QStringLiteral("kwin")));
    win::set_on_all_desktops(this, true);
    setOpacity(m_internalWindow->opacity());
    setSkipCloseAnimation(m_internalWindow->property(s_skipClosePropertyName).toBool());

    setupCompositing(false);
    updateColorScheme();

    win::block_geometry_updates(this, true);
    commitGeometry(m_internalWindow->geometry());
    updateDecoration(true);
    setFrameGeometry(win::client_rect_to_frame_rect(this, m_internalWindow->geometry()));
    setGeometryRestore(frameGeometry());
    win::block_geometry_updates(this, false);

    m_internalWindow->installEventFilter(this);
}

InternalClient::~InternalClient()
{
}

win::control* InternalClient::control() const
{
    return m_control.get();
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

QRect InternalClient::bufferGeometry() const
{
    return frameGeometry() - frameMargins();
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

QString InternalClient::captionNormal() const
{
    return m_captionNormal;
}

QString InternalClient::captionSuffix() const
{
    return m_captionSuffix;
}

QSize InternalClient::clientSize() const
{
    return m_clientSize;
}

void InternalClient::debug(QDebug &stream) const
{
    stream.nospace() << "\'InternalClient:" << m_internalWindow << "\'";
}

QRect InternalClient::transparentRect() const
{
    return QRect();
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

win::maximize_mode InternalClient::maximizeMode() const
{
    return win::maximize_mode::restore;
}

QRect InternalClient::geometryRestore() const
{
    return m_maximizeRestoreGeometry;
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

void InternalClient::resizeWithChecks(QSize const& size, win::force_geometry force)
{
    Q_UNUSED(force)
    if (!m_internalWindow) {
        return;
    }

    auto w = size.width();
    auto h = size.height();
    QRect area = workspace()->clientArea(WorkArea, this);

    // don't allow growing larger than workarea
    if (w > area.width()) {
        w = area.width();
    }
    if (h > area.height()) {
        h = area.height();
    }
    setFrameGeometry(QRect(pos(), QSize(w, h)));
}

void InternalClient::setFrameGeometry(const QRect &rect, win::force_geometry force)
{
    if (control()->geometry_updates_blocked()) {
        set_frame_geometry(rect);
        if (control()->pending_geometry_update() == win::pending_geometry::forced) {
            // Maximum, nothing needed.
        } else if (force == win::force_geometry::yes) {
            control()->set_pending_geometry_update(win::pending_geometry::forced);
        } else {
            control()->set_pending_geometry_update(win::pending_geometry::normal);
        }
        return;
    }

    if (control()->pending_geometry_update() != win::pending_geometry::none) {
        // Reset geometry to the one before blocking, so that we can compare properly.
        set_frame_geometry(control()->frame_geometry_before_update_blocking());
    }

    if (frameGeometry() == rect) {
        return;
    }

    auto const newClientGeometry = win::frame_rect_to_client_rect(this, rect);

    if (m_clientSize == newClientGeometry.size()) {
        commitGeometry(rect);
    } else {
        requestGeometry(rect);
    }
}

void InternalClient::setGeometryRestore(const QRect &rect)
{
    m_maximizeRestoreGeometry = rect;
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
    const QRect oldClientGeometry = oldFrameGeometry - frameMargins();

    win::geometry_updates_blocker blocker(this);

    if (force) {
        control()->destroy_decoration();
    }

    if (!noBorder()) {
        createDecoration(oldClientGeometry);
    } else {
        control()->destroy_decoration();
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
    if (control()->move_resize().enabled) {
        leaveMoveResize();
    }

    auto deleted = create_remnant(this);
    emit windowClosed(this, deleted);

    control()->destroy_decoration();

    workspace()->removeInternalClient(this);

    deleted->remnant()->unref();
    m_internalWindow = nullptr;

    delete this;
}

void InternalClient::present(const QSharedPointer<QOpenGLFramebufferObject> fbo)
{
    Q_ASSERT(m_internalImage.isNull());

    const QSize bufferSize = fbo->size() / bufferScale();

    commitGeometry(QRect(pos(), sizeForClientSize(bufferSize)));
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

    commitGeometry(QRect(pos(), sizeForClientSize(bufferSize)));
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
    requestGeometry(control()->move_resize().geometry);
}

void InternalClient::updateCaption()
{
    const QString oldSuffix = m_captionSuffix;
    const auto shortcut = win::shortcut_caption_suffix(this);
    m_captionSuffix = shortcut;
    if ((!win::is_special_window(this) || win::is_toolbar(this))
            && win::find_client_with_same_caption(static_cast<Toplevel*>(this))) {
        int i = 2;
        do {
            m_captionSuffix = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
            i++;
        } while (win::find_client_with_same_caption(static_cast<Toplevel*>(this)));
    }
    if (m_captionSuffix != oldSuffix) {
        emit captionChanged();
    }
}

void InternalClient::createDecoration(const QRect &rect)
{
    KDecoration2::Decoration *decoration = Decoration::DecorationBridge::self()->createDecoration(this);
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
                emit geometryShapeChanged(this, oldGeometry);
            }
        );
    }

    const QRect oldFrameGeometry = frameGeometry();

    control()->deco().decoration = decoration;
    setFrameGeometry(win::client_rect_to_frame_rect(this, rect));

    emit geometryShapeChanged(this, oldFrameGeometry);
}

void InternalClient::requestGeometry(const QRect &rect)
{
    if (m_internalWindow) {
        m_internalWindow->setGeometry(win::frame_rect_to_client_rect(this, rect));
    }
}

void InternalClient::commitGeometry(const QRect &rect)
{
    if (frameGeometry() == rect && control()->pending_geometry_update() == win::pending_geometry::none) {
        return;
    }

    set_frame_geometry(rect);

    m_clientSize = win::frame_rect_to_client_rect(this, frameGeometry()).size();

    addWorkspaceRepaint(visibleRect());
    syncGeometryToInternalWindow();

    const QRect oldGeometry = control()->frame_geometry_before_update_blocking();
    control()->update_geometry_before_update_blocking();
    emit geometryShapeChanged(this, oldGeometry);

    if (win::is_resize(this)) {
        win::perform_move_resize(this);
    }
}

void InternalClient::setCaption(const QString &caption)
{
    if (m_captionNormal == caption) {
        return;
    }

    m_captionNormal = caption;

    const QString oldCaptionSuffix = m_captionSuffix;
    updateCaption();

    if (m_captionSuffix == oldCaptionSuffix) {
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

void InternalClient::syncGeometryToInternalWindow()
{
    if (m_internalWindow->geometry() == win::frame_rect_to_client_rect(this, frameGeometry())) {
        return;
    }

    QTimer::singleShot(0, this, [this] { requestGeometry(frameGeometry()); });
}

void InternalClient::updateInternalWindowGeometry()
{
    if (control()->move_resize().enabled) {
        return;
    }

    commitGeometry(win::client_rect_to_frame_rect(this, m_internalWindow->geometry()));
}

}
