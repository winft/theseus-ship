/*
    SPDX-FileCopyrightText: 2019 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "desktop_set.h"
#include "geo_block.h"
#include "placement.h"
#include "rules/update.h"
#include "shortcut_set.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
#include "wayland/scene.h"
#include "wayland/surface.h"
#include "window_geometry.h"
#include "window_metadata.h"
#include "window_qobject.h"
#include "window_release.h"
#include "window_render_data.h"
#include "window_topology.h"

#include "render/wayland/buffer.h"
#include "render/window.h"

namespace KWin::win
{

template<typename Window>
class internal_control : public control<Window>
{
public:
    using control_t = win::control<Window>;

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

        auto const client_geo = win::frame_to_client_rect(m_client, m_client->geo.frame);
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
                window.window_type = static_cast<win::win_type>(
                    window.m_internalWindow->property("kwin_windowType").template value<int>());
                update_space_areas(window.space);
            }
        }
        return false;
    }

private:
    Window& window;
};

template<typename Space>
class internal_window
{
public:
    using space_t = Space;
    using type = internal_window<Space>;
    using qobject_t = win::window_qobject;
    using render_t = typename space_t::base_t::render_t::window_t;
    using output_t = typename Space::base_t::output_t;

    constexpr static bool is_toplevel{false};

    internal_window(win::remnant remnant, Space& space)
        : qobject{std::make_unique<window_qobject>()}
        , meta{++space.window_id}
        , transient{std::make_unique<win::transient<type>>(this)}
        , remnant{std::move(remnant)}
        , space{space}
    {
        this->space.windows_map.insert({this->meta.signal_id, this});
    }

    internal_window(QWindow* window, Space& space)
        : qobject{std::make_unique<internal_window_qobject<type>>(*this)}
        , singleton{std::make_unique<internal_window_singleton>(
              [this] { destroyClient(); },
              [this](auto fbo) { present(fbo); },
              [this](auto const& image, auto const& damage) { present(image, damage); })}
        , meta{++space.window_id}
        , transient{std::make_unique<win::transient<type>>(this)}
        , m_internalWindow(window)
        , synced_geo(window->geometry())
        , m_internalWindowFlags(window->flags())
        , space{space}
    {
        this->space.windows_map.insert({this->meta.signal_id, this});
        auto& qwin = this->qobject;

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
            window_type = static_cast<win::win_type>(windowType.value<int>());
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
        this->geo.restore.max = this->geo.frame;
        win::block_geometry_updates(this, false);

        m_internalWindow->installEventFilter(qwin.get());
    }

    ~internal_window()
    {
        this->space.windows_map.erase(this->meta.signal_id);
    }

    void setupCompositing()
    {
        wayland::setup_compositing(*this);
    }

    void add_scene_window_addon()
    {
        auto setup_buffer = [](auto& buffer) {
            using buffer_integration_t = typename Space::base_t::render_t::buffer_t;
            auto win_integrate = std::make_unique<buffer_integration_t>(buffer);

            auto update_helper = [&buffer]() {
                auto win = std::get<type*>(*buffer.window->ref_win);
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

    qreal bufferScale() const
    {
        return this->remnant ? this->remnant->data.buffer_scale : buffer_scale_internal();
    }

    QSize resizeIncrements() const
    {
        return {1, 1};
    }

    void debug(QDebug& stream) const
    {
        if (this->remnant) {
            stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
            return;
        }
        stream.nospace() << "\'internal_window:" << m_internalWindow << "\'";
    }

    win::win_type windowType() const
    {
        return window_type;
    }

    win::win_type get_window_type_direct() const
    {
        return window_type;
    }

    // TODO(romangg): Remove
    xcb_timestamp_t userTime() const
    {
        return XCB_TIME_CURRENT_TIME;
    }

    pid_t pid() const
    {
        return 0;
    }

    double opacity() const
    {
        return this->remnant ? this->remnant->data.opacity : m_opacity;
    }

    void setOpacity(double opacity)
    {
        if (m_opacity == opacity) {
            return;
        }

        const double oldOpacity = m_opacity;
        m_opacity = opacity;

        Q_EMIT this->qobject->opacityChanged(oldOpacity);
    }

    QSize basicUnit() const
    {
        return {1, 1};
    }

    void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
    {
        if (this->remnant) {
            return this->remnant->data.layout_decoration_rects(left, top, right, bottom);
        }
        win::layout_decoration_rects(this, left, top, right, bottom);
    }

    QRegion render_region() const
    {
        if (this->remnant) {
            return this->remnant->data.render_region;
        }

        auto const render_geo = win::render_geometry(this);
        return QRegion(0, 0, render_geo.width(), render_geo.height());
    }

    bool providesContextHelp() const
    {
        return false;
    }

    void killWindow()
    {
        // We don't kill our internal windows.
    }

    bool is_popup_end() const
    {
        return this->remnant ? this->remnant->data.was_popup_window
                             : m_internalWindowFlags.testFlag(Qt::Popup);
    }

    layer layer_for_dock() const
    {
        return win::layer_for_dock(*this);
    }

    QByteArray windowRole() const
    {
        return {};
    }

    xcb_window_t frameId() const
    {
        return XCB_WINDOW_NONE;
    }

    void closeWindow()
    {
        if (m_internalWindow) {
            m_internalWindow->hide();
        }
    }

    bool isCloseable() const
    {
        return true;
    }

    bool isMaximizable() const
    {
        return false;
    }

    bool isMinimizable() const
    {
        return false;
    }

    bool isMovable() const
    {
        return true;
    }

    bool isMovableAcrossScreens() const
    {
        return true;
    }

    bool isResizable() const
    {
        return true;
    }

    bool placeable() const
    {
        return !m_internalWindowFlags.testFlag(Qt::BypassWindowManagerHint)
            && !m_internalWindowFlags.testFlag(Qt::Popup);
    }

    // TODO(romangg): Only a default value, but it is needed in several functions. Remove somehow?
    win::maximize_mode maximizeMode() const
    {
        return win::maximize_mode::restore;
    }

    // TODO(romangg): Only a noop, but it is needed in several functions. Remove somehow?
    void update_maximized(win::maximize_mode /*mode*/)
    {
    }

    void setShortcutInternal()
    {
        updateCaption();
        win::window_shortcut_updated(this->space, this);
    }

    void updateWindowRules(win::rules::type selection)
    {
        if (this->space.rule_book->areUpdatesDisabled()) {
            return;
        }
        win::rules::update_window(control->rules, *this, static_cast<int>(selection));
    }

    QSize minSize() const
    {
        return control->rules.checkMinSize(QSize(0, 0));
    }

    QSize maxSize() const
    {
        return control->rules.checkMaxSize(QSize(INT_MAX, INT_MAX));
    }

    bool noBorder() const
    {
        if (this->remnant) {
            return this->remnant->data.no_border;
        }
        return m_userNoBorder || m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
            || m_internalWindowFlags.testFlag(Qt::Popup);
    }

    bool userCanSetNoBorder() const
    {
        return !m_internalWindowFlags.testFlag(Qt::FramelessWindowHint)
            || m_internalWindowFlags.testFlag(Qt::Popup);
    }

    bool wantsInput() const
    {
        return false;
    }

    bool isInternal() const
    {
        return true;
    }

    bool isLockScreen() const
    {
        if (m_internalWindow) {
            return m_internalWindow->property("org_kde_ksld_emergency").toBool();
        }
        return false;
    }

    bool isShown() const
    {
        return this->render_data.ready_for_painting;
    }

    bool isHiddenInternal() const
    {
        return false;
    }

    void hideClient(bool /*hide*/)
    {
    }

    void leaveMoveResize()
    {
        win::leave_move_resize(*this);
    }

    void setFrameGeometry(QRect const& rect)
    {
        this->geo.update.frame = rect;

        if (this->geo.update.block) {
            this->geo.update.pending = win::pending_geometry::normal;
            return;
        }

        this->geo.update.pending = win::pending_geometry::none;

        if (synced_geo != win::frame_to_client_rect(this, rect)) {
            requestGeometry(rect);
            return;
        }

        do_set_geometry(rect);
    }

    void apply_restore_geometry(QRect const& restore_geo)
    {
        setFrameGeometry(rectify_restore_geometry(this, restore_geo));
    }

    void restore_geometry_from_fullscreen()
    {
    }

    bool hasStrut() const
    {
        return false;
    }

    bool supportsWindowRules() const
    {
        return false;
    }

    void takeFocus()
    {
    }

    bool userCanSetFullScreen() const
    {
        return false;
    }

    void setFullScreen(bool /*set*/, bool /*user*/ = true)
    {
    }

    void handle_update_fullscreen(bool /*full*/)
    {
    }

    void setNoBorder(bool set)
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

    void checkNoBorder()
    {
        setNoBorder(false);
    }

    void handle_update_no_border()
    {
        setNoBorder(this->geo.update.max_mode == maximize_mode::full);
    }

    void updateDecoration(bool check_workspace_pos, bool force = false)
    {
        if (!force && (win::decoration(this) != nullptr) == !noBorder()) {
            return;
        }

        const QRect oldFrameGeometry = this->geo.frame;
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

    void updateColorScheme()
    {
        win::set_color_scheme(this, QString());
    }

    void showOnScreenEdge()
    {
    }

    void checkTransient(type* /*window*/)
    {
    }

    bool belongsToDesktop() const
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
            scene_add_remnant(*deleted);
        }
        Q_EMIT this->qobject->closed();

        this->control->destroy_decoration();

        remove_window_from_lists(this->space, this);
        this->space.stacking.order.update_count();
        update_space_areas(this->space);
        Q_EMIT this->space.qobject->internalClientRemoved(this->meta.signal_id);

        m_internalWindow = nullptr;

        if (deleted) {
            deleted->remnant->unref();
            delete this;
        } else {
            delete_window_from_space(this->space, *this);
        }
    }

    void present(std::shared_ptr<QOpenGLFramebufferObject> const& fbo)
    {
        assert(buffers.image.isNull());

        const QSize bufferSize = fbo->size() / buffer_scale_internal();

        this->setFrameGeometry(QRect(this->geo.pos(), win::client_to_frame_size(this, bufferSize)));
        markAsMapped();

        if (buffers.fbo != fbo) {
            discard_buffer(*this);
            buffers.fbo = fbo;
        }

        set_bit_depth(*this, 32);
        add_full_damage(*this);
        add_full_repaint(*this);
    }

    void present(const QImage& image, const QRegion& damage)
    {
        assert(!buffers.fbo);

        const QSize bufferSize = image.size() / buffer_scale_internal();

        this->setFrameGeometry(QRect(this->geo.pos(), win::client_to_frame_size(this, bufferSize)));
        markAsMapped();

        if (buffers.image.size() != image.size()) {
            discard_buffer(*this);
        }

        buffers.image = image;

        set_bit_depth(*this, 32);
        wayland::handle_surface_damage(*this, damage);
    }

    QWindow* internalWindow() const
    {
        return m_internalWindow;
    }

    bool has_pending_repaints() const
    {
        return this->isShown() && !repaints(*this).isEmpty();
    }

    bool acceptsFocus() const
    {
        return false;
    }

    bool belongsToSameApplication(type const* other, win::same_client_check /*checks*/) const
    {
        return other != nullptr;
    }

    void doResizeSync()
    {
        requestGeometry(this->control->move_resize.geometry);
    }

    void updateCaption()
    {
        auto const oldSuffix = this->meta.caption.suffix;
        const auto shortcut = win::shortcut_caption_suffix(this);
        this->meta.caption.suffix = shortcut;
        if ((!win::is_special_window(this) || win::is_toolbar(this))
            && win::find_client_with_same_caption(this)) {
            int i = 2;
            do {
                this->meta.caption.suffix
                    = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
                i++;
            } while (win::find_client_with_same_caption(this));
        }
        if (this->meta.caption.suffix != oldSuffix) {
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
        this->control->deco.window = new deco::window<typename Space::window_t>(this);
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
                                 auto const old_geo = this->geo.frame;
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
        if (this->meta.caption.normal == cap) {
            return;
        }

        this->meta.caption.normal = cap;

        auto const oldCaptionSuffix = this->meta.caption.suffix;
        updateCaption();

        if (this->meta.caption.suffix == oldCaptionSuffix) {
            Q_EMIT this->qobject->captionChanged();
        }
    }

    void markAsMapped()
    {
        if (this->render_data.ready_for_painting) {
            return;
        }

        set_ready_for_painting(*this);

        this->space.windows.push_back(this);

        setup_space_window_connections(&this->space, this);
        update_layer(this);

        if (placeable()) {
            auto const area = space_window_area(
                this->space, PlacementArea, get_current_output(this->space), get_desktop(*this));
            place_in_area(this, area);
        }

        this->space.stacking.order.update_count();
        update_space_areas(this->space);

        Q_EMIT this->space.qobject->internalClientAdded(this->meta.signal_id);
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
        auto const old_frame_geo = this->geo.frame;

        if (old_frame_geo == frame_geo) {
            return;
        }

        this->geo.frame = frame_geo;

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

    std::unique_ptr<qobject_t> qobject;
    std::unique_ptr<internal_window_singleton> singleton;

    win::window_metadata meta;
    win::window_geometry geo;
    win::window_topology<output_t> topo;
    win::window_render_data<output_t> render_data;

    std::unique_ptr<win::transient<type>> transient;
    std::unique_ptr<win::control<type>> control;
    std::unique_ptr<render_t> render;
    std::optional<win::remnant> remnant;

    struct {
        std::shared_ptr<QOpenGLFramebufferObject> fbo;
        QImage image;
    } buffers;

    QWindow* m_internalWindow = nullptr;
    QRect synced_geo;
    double m_opacity = 1.0;
    win::win_type window_type{win_type::normal};
    Qt::WindowFlags m_internalWindowFlags = Qt::WindowFlags();
    bool m_userNoBorder = false;
    bool is_outline{false};
    bool skip_close_animation{false};

    Space& space;
};

}
