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
#pragma once

#include "desktop_set.h"
#include "geo_block.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
#include "wayland/scene.h"
#include "wayland/surface.h"
#include "window_release.h"

#include "render/wayland/buffer.h"
#include "toplevel.h"

#include <NETWM>

namespace KWin::win
{

template<typename Window>
class internal_control : public control<typename Window::abstract_type>
{
public:
    using control_t = win::control<typename Window::abstract_type>;

    internal_control(Window* client)
        : control_t(client)
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
        control_t::destroy_decoration();
        m_client->setFrameGeometry(client_geo);
    }

private:
    Window* m_client;
};

constexpr char internal_skip_close_animation_name[]{"KWIN_SKIP_CLOSE_ANIMATION"};

template<typename Window>
class internal_window_qobject : public window_qobject
{
public:
    internal_window_qobject(Window& window)
        : window{window}
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window.m_internalWindow && event->type() == QEvent::DynamicPropertyChange) {
            auto pe = static_cast<QDynamicPropertyChangeEvent*>(event);
            if (pe->propertyName() == internal_skip_close_animation_name) {
                set_skip_close_animation(
                    window,
                    window.m_internalWindow->property(internal_skip_close_animation_name).toBool());
            }
            if (pe->propertyName() == "kwin_windowType") {
                window.window_type = window.m_internalWindow->property("kwin_windowType")
                                         .template value<NET::WindowType>();
                update_space_areas(window.space);
            }
        }
        return false;
    }

private:
    Window& window;
};

template<typename Space>
class internal_window : public Toplevel<Space>
{
public:
    using type = internal_window<Space>;
    using abstract_type = Toplevel<Space>;

    constexpr static bool is_toplevel{false};

    internal_window(win::remnant remnant, Space& space)
        : Toplevel<Space>(std::move(remnant), space)
    {
        this->qobject = std::make_unique<window_qobject>();
    }

    internal_window(QWindow* window, Space& space)
        : Toplevel<Space>(space)
        , singleton{std::make_unique<internal_window_singleton>(
              [this] { destroyClient(); },
              [this](auto fbo) { present(fbo); },
              [this](auto const& image, auto const& damage) { present(image, damage); })}
        , m_internalWindow(window)
        , synced_geo(window->geometry())
        , m_internalWindowFlags(window->flags())
    {
        auto& qwin = this->qobject;
        qwin = std::make_unique<internal_window_qobject<type>>(*this);

        this->control = std::make_unique<internal_control<type>>(this);

        QObject::connect(m_internalWindow, &QWindow::xChanged, qwin.get(), [this] {
            updateInternalWindowGeometry();
        });
        QObject::connect(m_internalWindow, &QWindow::yChanged, qwin.get(), [this] {
            updateInternalWindowGeometry();
        });
        QObject::connect(m_internalWindow, &QWindow::widthChanged, qwin.get(), [this] {
            updateInternalWindowGeometry();
        });
        QObject::connect(m_internalWindow, &QWindow::heightChanged, qwin.get(), [this] {
            updateInternalWindowGeometry();
        });
        QObject::connect(m_internalWindow,
                         &QWindow::windowTitleChanged,
                         qwin.get(),
                         [this](auto const& cap) { setCaption(cap); });
        QObject::connect(m_internalWindow,
                         &QWindow::opacityChanged,
                         qwin.get(),
                         [this](auto opacity) { setOpacity(opacity); });
        QObject::connect(
            m_internalWindow, &QWindow::destroyed, qwin.get(), [this] { destroyClient(); });

        QObject::connect(qwin.get(), &window_qobject::opacityChanged, qwin.get(), [this] {
            add_full_repaint(*this);
        });

        const QVariant windowType = m_internalWindow->property("kwin_windowType");
        if (!windowType.isNull()) {
            window_type = windowType.value<NET::WindowType>();
        }

        setCaption(m_internalWindow->title());
        this->control->icon = QIcon::fromTheme(QStringLiteral("kwin"));

        set_on_all_desktops(this, true);
        setOpacity(m_internalWindow->opacity());
        set_skip_close_animation(
            *this, m_internalWindow->property(internal_skip_close_animation_name).toBool());
        this->is_outline = m_internalWindow->property("__kwin_outline").toBool();

        setupCompositing();
        updateColorScheme();

        win::block_geometry_updates(this, true);
        updateDecoration(true);
        setFrameGeometry(win::client_to_frame_rect(this, m_internalWindow->geometry()));
        this->restore_geometries.maximize = this->frameGeometry();
        win::block_geometry_updates(this, false);

        m_internalWindow->installEventFilter(qwin.get());
    }

    void setupCompositing() override
    {
        wayland::setup_compositing(*this);
    }

    void add_scene_window_addon() override
    {
        auto setup_buffer = [](auto& buffer) {
            using scene_t = typename Space::base_t::render_t::compositor_t::scene_t;
            using buffer_integration_t
                = render::wayland::buffer_win_integration<typename scene_t::buffer_t>;

            auto win_integrate = std::make_unique<buffer_integration_t>(buffer);
            auto update_helper = [&buffer]() {
                auto win = static_cast<internal_window*>(buffer.window->ref_win);
                auto& win_integrate = static_cast<buffer_integration_t&>(*buffer.win_integration);
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

        this->render->win_integration.setup_buffer = setup_buffer;
    }

    qreal bufferScale() const override
    {
        return this->remnant ? this->remnant->data.buffer_scale : buffer_scale_internal();
    }

    void debug(QDebug& stream) const override
    {
        if (this->remnant) {
            stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
            return;
        }
        stream.nospace() << "\'internal_window:" << m_internalWindow << "\'";
    }

    NET::WindowType windowType() const override
    {
        return window_type;
    }

    double opacity() const override
    {
        return this->remnant ? this->remnant->data.opacity : m_opacity;
    }

    void setOpacity(double opacity) override
    {
        if (m_opacity == opacity) {
            return;
        }

        const double oldOpacity = m_opacity;
        m_opacity = opacity;

        Q_EMIT this->qobject->opacityChanged(oldOpacity);
    }

    void killWindow() override
    {
        // We don't kill our internal windows.
    }

    bool is_popup_end() const override
    {
        return this->remnant ? this->remnant->data.was_popup_window
                             : m_internalWindowFlags.testFlag(Qt::Popup);
    }

    QByteArray windowRole() const override
    {
        return {};
    }

    void closeWindow() override
    {
        if (m_internalWindow) {
            m_internalWindow->hide();
        }
    }

    bool isCloseable() const override
    {
        return true;
    }

    bool isMaximizable() const override
    {
        return false;
    }

    bool isMinimizable() const override
    {
        return false;
    }

    bool isMovable() const override
    {
        return true;
    }

    bool isMovableAcrossScreens() const override
    {
        return true;
    }

    bool isResizable() const override
    {
        return true;
    }

    bool placeable() const
    {
        return !m_internalWindowFlags.testFlag(Qt::BypassWindowManagerHint)
            && !m_internalWindowFlags.testFlag(Qt::Popup);
    }

    bool noBorder() const override
    {
        if (this->remnant) {
            return this->remnant->data.no_border;
        }
        return m_userNoBorder || m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
            || m_internalWindowFlags.testFlag(Qt::Popup);
    }

    bool userCanSetNoBorder() const override
    {
        return !m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
            || m_internalWindowFlags.testFlag(Qt::Popup);
    }

    bool wantsInput() const override
    {
        return false;
    }

    bool isInternal() const override
    {
        return true;
    }

    bool isLockScreen() const override
    {
        if (m_internalWindow) {
            return m_internalWindow->property("org_kde_ksld_emergency").toBool();
        }
        return false;
    }

    bool isShown() const override
    {
        return this->ready_for_painting;
    }

    bool isHiddenInternal() const override
    {
        return false;
    }

    void hideClient(bool /*hide*/) override
    {
    }

    void setFrameGeometry(QRect const& rect) override
    {
        this->geometry_update.frame = rect;

        if (this->geometry_update.block) {
            this->geometry_update.pending = win::pending_geometry::normal;
            return;
        }

        this->geometry_update.pending = win::pending_geometry::none;

        if (synced_geo != win::frame_to_client_rect(this, rect)) {
            requestGeometry(rect);
            return;
        }

        do_set_geometry(rect);
    }

    void apply_restore_geometry(QRect const& restore_geo) override
    {
        setFrameGeometry(rectify_restore_geometry(this, restore_geo));
    }

    void restore_geometry_from_fullscreen() override
    {
    }

    bool hasStrut() const override
    {
        return false;
    }

    bool supportsWindowRules() const override
    {
        return false;
    }

    void takeFocus() override
    {
    }

    bool userCanSetFullScreen() const override
    {
        return false;
    }

    void setFullScreen(bool /*set*/, bool /*user*/ = true) override
    {
    }

    void handle_update_fullscreen(bool /*full*/) override
    {
    }

    void setNoBorder(bool set) override
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

    void handle_update_no_border() override
    {
        setNoBorder(this->geometry_update.max_mode == maximize_mode::full);
    }

    void updateDecoration(bool check_workspace_pos, bool force = false) override
    {
        if (!force && (win::decoration(this) != nullptr) == !noBorder()) {
            return;
        }

        const QRect oldFrameGeometry = this->frameGeometry();
        const QRect oldClientGeometry = oldFrameGeometry - win::frame_margins(this);

        win::geometry_updates_blocker blocker(this);

        if (force) {
            this->control->destroy_decoration();
        }

        if (!noBorder()) {
            createDecoration(oldClientGeometry);
        } else {
            this->control->destroy_decoration();
        }

        win::update_shadow(this);

        if (check_workspace_pos) {
            win::check_workspace_position(this, oldFrameGeometry, -2, oldClientGeometry);
        }
    }

    void updateColorScheme() override
    {
        win::set_color_scheme(this, QString());
    }

    void showOnScreenEdge() override
    {
    }

    void checkTransient(Toplevel<Space>* /*window*/) override
    {
    }

    bool belongsToDesktop() const override
    {
        return false;
    }

    void destroyClient()
    {
        if (this->control->move_resize.enabled) {
            this->leaveMoveResize();
        }

        auto deleted = win::create_remnant_window<internal_window>(*this);
        if (deleted) {
            transfer_remnant_data(*this, *deleted);
            space_add_remnant(*this, *deleted);
        }
        Q_EMIT this->qobject->closed();

        this->control->destroy_decoration();

        remove_window_from_lists(this->space, this);
        this->space.stacking.order.update_count();
        update_space_areas(this->space);
        Q_EMIT this->space.qobject->internalClientRemoved(this->signal_id);

        m_internalWindow = nullptr;

        if (deleted) {
            deleted->remnant->unref();
            delete this;
        } else {
            delete_window_from_space(this->space, this);
        }
    }

    void present(std::shared_ptr<QOpenGLFramebufferObject> const& fbo)
    {
        assert(buffers.image.isNull());

        const QSize bufferSize = fbo->size() / buffer_scale_internal();

        this->setFrameGeometry(QRect(this->pos(), win::client_to_frame_size(this, bufferSize)));
        markAsMapped();

        if (buffers.fbo != fbo) {
            discard_buffer(*this);
            buffers.fbo = fbo;
        }

        this->setDepth(32);
        add_full_damage(*this);
        add_full_repaint(*this);
    }

    void present(const QImage& image, const QRegion& damage)
    {
        assert(!buffers.fbo);

        const QSize bufferSize = image.size() / buffer_scale_internal();

        this->setFrameGeometry(QRect(this->pos(), win::client_to_frame_size(this, bufferSize)));
        markAsMapped();

        if (buffers.image.size() != image.size()) {
            discard_buffer(*this);
        }

        buffers.image = image;

        this->setDepth(32);
        wayland::handle_surface_damage(*this, damage);
    }

    QWindow* internalWindow() const
    {
        return m_internalWindow;
    }

    bool has_pending_repaints() const override
    {
        return this->isShown() && Toplevel<Space>::has_pending_repaints();
    }

    struct {
        std::shared_ptr<QOpenGLFramebufferObject> fbo;
        QImage image;
    } buffers;

    std::unique_ptr<internal_window_singleton> singleton;

    bool acceptsFocus() const override
    {
        return false;
    }

    bool belongsToSameApplication(Toplevel<Space> const* other,
                                  win::same_client_check /*checks*/) const override
    {
        return dynamic_cast<internal_window const*>(other) != nullptr;
    }

    void doResizeSync() override
    {
        requestGeometry(this->control->move_resize.geometry);
    }

    void updateCaption() override
    {
        auto const oldSuffix = this->caption.suffix;
        const auto shortcut = win::shortcut_caption_suffix(this);
        this->caption.suffix = shortcut;
        if ((!win::is_special_window(this) || win::is_toolbar(this))
            && win::find_client_with_same_caption(static_cast<Toplevel<Space>*>(this))) {
            int i = 2;
            do {
                this->caption.suffix
                    = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
                i++;
            } while (win::find_client_with_same_caption(static_cast<Toplevel<Space>*>(this)));
        }
        if (this->caption.suffix != oldSuffix) {
            Q_EMIT this->qobject->captionChanged();
        }
    }

    double buffer_scale_internal() const
    {
        if (m_internalWindow) {
            return m_internalWindow->devicePixelRatio();
        }
        return 1;
    }

    void createDecoration(const QRect& rect)
    {
        this->control->deco.window = new deco::window<Toplevel<Space>>(this);
        auto decoration = this->space.deco->createDecoration(this->control->deco.window);

        if (decoration) {
            QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);
            QObject::connect(decoration,
                             &KDecoration2::Decoration::shadowChanged,
                             this->qobject.get(),
                             [this] { win::update_shadow(this); });
            QObject::connect(decoration,
                             &KDecoration2::Decoration::bordersChanged,
                             this->qobject.get(),
                             [this]() {
                                 win::geometry_updates_blocker blocker(this);
                                 auto const old_geo = this->frameGeometry();
                                 win::check_workspace_position(this, old_geo);
                                 discard_shape(*this);
                                 this->control->deco.client->update_size();
                             });
        }

        this->control->deco.decoration = decoration;
        this->setFrameGeometry(win::client_to_frame_rect(this, rect));
        discard_shape(*this);
    }

    void setCaption(QString const& cap)
    {
        if (this->caption.normal == cap) {
            return;
        }

        this->caption.normal = cap;

        auto const oldCaptionSuffix = this->caption.suffix;
        updateCaption();

        if (this->caption.suffix == oldCaptionSuffix) {
            Q_EMIT this->qobject->captionChanged();
        }
    }

    void markAsMapped()
    {
        if (this->ready_for_painting) {
            return;
        }

        set_ready_for_painting(*this);

        this->space.windows.push_back(this);

        setup_space_window_connections(&this->space, this);
        update_layer(this);

        if (placeable()) {
            auto const area = space_window_area(
                this->space, PlacementArea, get_current_output(this->space), this->desktop());
            place(this, area);
        }

        this->space.stacking.order.update_count();
        update_space_areas(this->space);

        Q_EMIT this->space.qobject->internalClientAdded(this->signal_id);
    }

    void requestGeometry(const QRect& rect)
    {
        if (m_internalWindow) {
            m_internalWindow->setGeometry(win::frame_to_client_rect(this, rect));
            synced_geo = rect;
        }
    }

    void do_set_geometry(QRect const& frame_geo)
    {
        auto const old_frame_geo = this->frameGeometry();

        if (old_frame_geo == frame_geo) {
            return;
        }

        this->set_frame_geometry(frame_geo);

        if (win::is_resize(this)) {
            win::perform_move_resize(this);
        }

        this->space.base.render->compositor->addRepaint(visible_rect(this));

        Q_EMIT this->qobject->frame_geometry_changed(old_frame_geo);
    }

    void updateInternalWindowGeometry()
    {
        if (this->control->move_resize.enabled) {
            return;
        }
        if (!m_internalWindow) {
            // Might be called in dtor of QWindow
            // TODO: Can this be ruled out through other means?
            return;
        }

        do_set_geometry(win::client_to_frame_rect(this, m_internalWindow->geometry()));
    }

    QWindow* m_internalWindow = nullptr;
    QRect synced_geo;
    double m_opacity = 1.0;
    NET::WindowType window_type{NET::Normal};
    Qt::WindowFlags m_internalWindowFlags = Qt::WindowFlags();
    bool m_userNoBorder = false;
};

}
