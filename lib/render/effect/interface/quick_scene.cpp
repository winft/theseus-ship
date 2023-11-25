/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "quick_scene.h"

#include "effects_handler.h"

#include <base/logging.h>

#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlIncubator>
#include <QQuickItem>
#include <QQuickWindow>

namespace KWin
{

static QHash<QQuickWindow*, QuickSceneView*> s_views;

class QuickSceneViewIncubator : public QQmlIncubator
{
public:
    QuickSceneViewIncubator(
        QuickSceneEffect* effect,
        EffectScreen const* screen,
        const std::function<void(QuickSceneViewIncubator*)>& statusChangedCallback)
        : QQmlIncubator(QQmlIncubator::Asynchronous)
        , m_effect(effect)
        , m_screen(screen)
        , m_statusChangedCallback(statusChangedCallback)
    {
    }

    std::unique_ptr<QuickSceneView> result()
    {
        return std::move(m_view);
    }

    void setInitialState(QObject* object) override
    {
        m_view = std::make_unique<QuickSceneView>(m_effect, m_screen);
        m_view->setAutomaticRepaint(false);
        m_view->setRootItem(qobject_cast<QQuickItem*>(object));
    }

    void statusChanged(QQmlIncubator::Status /*status*/) override
    {
        m_statusChangedCallback(this);
    }

private:
    QuickSceneEffect* m_effect;
    EffectScreen const* m_screen;
    std::function<void(QuickSceneViewIncubator*)> m_statusChangedCallback;
    std::unique_ptr<QuickSceneView> m_view;
};

class QuickSceneEffectPrivate
{
public:
    static QuickSceneEffectPrivate* get(QuickSceneEffect* effect)
    {
        return effect->d.get();
    }
    bool isItemOnScreen(QQuickItem* item, EffectScreen const* screen) const;

    std::unique_ptr<QQmlComponent> delegate;
    QUrl source;
    std::map<EffectScreen const*, std::unique_ptr<QQmlContext>> contexts;
    std::map<EffectScreen const*, std::unique_ptr<QQmlIncubator>> incubators;
    std::map<EffectScreen const*, std::unique_ptr<QuickSceneView>> views;
    QPointer<QuickSceneView> mouseImplicitGrab;
    bool running = false;
    EffectScreen const* paintedScreen{nullptr};
};

bool QuickSceneEffectPrivate::isItemOnScreen(QQuickItem* item, EffectScreen const* screen) const
{
    if (!item || !screen || !views.contains(screen)) {
        return false;
    }

    auto const& view = views.at(screen);
    return item->window() == view->window();
}

QuickSceneView::QuickSceneView(QuickSceneEffect* effect, EffectScreen const* screen)
    : OffscreenQuickView(ExportMode::Texture, false)
    , m_effect(effect)
    , m_screen(screen)
{
    setGeometry(screen->geometry());
    connect(screen, &EffectScreen::geometryChanged, this, [this, screen]() {
        setGeometry(screen->geometry());
    });

    s_views.insert(window(), this);
}

QuickSceneView::~QuickSceneView()
{
    s_views.remove(window());
}

QQuickItem* QuickSceneView::rootItem() const
{
    return m_rootItem.get();
}

void QuickSceneView::setRootItem(QQuickItem* item)
{
    Q_ASSERT_X(item, "setRootItem", "root item cannot be null");
    m_rootItem.reset(item);
    m_rootItem->setParentItem(contentItem());

    auto updateSize = [this]() { m_rootItem->setSize(contentItem()->size()); };
    updateSize();
    connect(contentItem(), &QQuickItem::widthChanged, m_rootItem.get(), updateSize);
    connect(contentItem(), &QQuickItem::heightChanged, m_rootItem.get(), updateSize);
}

QuickSceneEffect* QuickSceneView::effect() const
{
    return m_effect;
}

EffectScreen const* QuickSceneView::screen() const
{
    return m_screen;
}

bool QuickSceneView::isDirty() const
{
    return m_dirty;
}

void QuickSceneView::markDirty()
{
    m_dirty = true;
}

void QuickSceneView::resetDirty()
{
    m_dirty = false;
}

void QuickSceneView::scheduleRepaint()
{
    markDirty();
    effects->addRepaint(geometry());
}

QuickSceneView* QuickSceneView::findView(QQuickItem* item)
{
    return s_views.value(item->window());
}

QuickSceneView* QuickSceneView::qmlAttachedProperties(QObject* object)
{
    QQuickItem* item = qobject_cast<QQuickItem*>(object);
    if (item) {
        if (QuickSceneView* view = findView(item)) {
            return view;
        }
    }
    qCWarning(KWIN_CORE) << "Could not find SceneView for" << object;
    return nullptr;
}

QuickSceneEffect::QuickSceneEffect(QObject* parent)
    : Effect(parent)
    , d(new QuickSceneEffectPrivate)
{
}

QuickSceneEffect::~QuickSceneEffect()
{
}

bool QuickSceneEffect::supported()
{
    return effects->isOpenGLCompositing();
}

void QuickSceneEffect::checkItemDraggedOutOfScreen(QQuickItem* item)
{
    auto const globalGeom
        = QRectF(item->mapToGlobal(QPointF(0, 0)), QSizeF(item->width(), item->height()));
    QList<EffectScreen const*> screens;

    for (auto const& [screen, view] : d->views) {
        if (!d->isItemOnScreen(item, screen)
            && screen->geometry().intersects(globalGeom.toRect())) {
            screens << screen;
        }
    }

    Q_EMIT itemDraggedOutOfScreen(item, screens);
}

void QuickSceneEffect::checkItemDroppedOutOfScreen(const QPointF& globalPos, QQuickItem* item)
{
    auto const it
        = std::find_if(d->views.begin(), d->views.end(), [this, globalPos, item](auto const& view) {
              auto screen = view.first;
              return !d->isItemOnScreen(item, screen)
                  && screen->geometry().contains(globalPos.toPoint());
          });
    if (it != d->views.end()) {
        Q_EMIT itemDroppedOutOfScreen(globalPos, item, it->first);
    }
}

bool QuickSceneEffect::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::CursorChange) {
        if (const QWindow* window = qobject_cast<QWindow*>(watched)) {
            effects->defineCursor(window->cursor().shape());
        }
    }
    return false;
}

bool QuickSceneEffect::isRunning() const
{
    return d->running;
}

void QuickSceneEffect::setRunning(bool running)
{
    if (d->running != running) {
        if (running) {
            startInternal();
        } else {
            stopInternal();
        }
    }
}

QUrl QuickSceneEffect::source() const
{
    return d->source;
}

void QuickSceneEffect::setSource(const QUrl& url)
{
    if (isRunning()) {
        qWarning() << "Cannot change QuickSceneEffect.source while running";
        return;
    }
    if (d->source != url) {
        d->source = url;
        d->delegate.reset();
    }
}

QQmlComponent* QuickSceneEffect::delegate() const
{
    return d->delegate.get();
}

void QuickSceneEffect::setDelegate(QQmlComponent* delegate)
{
    if (isRunning()) {
        qWarning() << "Cannot change QuickSceneEffect.source while running";
        return;
    }
    if (d->delegate.get() != delegate) {
        d->source = QUrl();
        d->delegate.reset(delegate);
        Q_EMIT delegateChanged();
    }
}

QuickSceneView* QuickSceneEffect::viewForScreen(EffectScreen const* screen) const
{
    auto const it = d->views.find(screen);
    return it == d->views.end() ? nullptr : it->second.get();
}

QuickSceneView* QuickSceneEffect::viewAt(const QPoint& pos) const
{
    auto const it = std::find_if(d->views.begin(), d->views.end(), [pos](auto const& view) {
        return view.second->geometry().contains(pos);
    });
    return it == d->views.end() ? nullptr : it->second.get();
}

QuickSceneView* QuickSceneEffect::activeView() const
{
    auto const it = std::find_if(d->views.begin(), d->views.end(), [](auto const& view) {
        return view.second->window()->activeFocusItem();
    });
    return it == d->views.end() ? d->views[effects->activeScreen()].get() : it->second.get();
}

KWin::QuickSceneView* QuickSceneEffect::getView(Qt::Edge edge)
{
    auto screenView = activeView();

    QuickSceneView* candidate = nullptr;

    for (auto const& [screen, view] : d->views) {
        switch (edge) {
        case Qt::LeftEdge:
            if (view->geometry().left() < screenView->geometry().left()) {
                // Look for the nearest view from the current
                if (!candidate || view->geometry().left() > candidate->geometry().left()
                    || (view->geometry().left() == candidate->geometry().left()
                        && view->geometry().top() > candidate->geometry().top())) {
                    candidate = view.get();
                }
            }
            break;
        case Qt::TopEdge:
            if (view->geometry().top() < screenView->geometry().top()) {
                if (!candidate || view->geometry().top() > candidate->geometry().top()
                    || (view->geometry().top() == candidate->geometry().top()
                        && view->geometry().left() > candidate->geometry().left())) {
                    candidate = view.get();
                }
            }
            break;
        case Qt::RightEdge:
            if (view->geometry().right() > screenView->geometry().right()) {
                if (!candidate || view->geometry().right() < candidate->geometry().right()
                    || (view->geometry().right() == candidate->geometry().right()
                        && view->geometry().top() > candidate->geometry().top())) {
                    candidate = view.get();
                }
            }
            break;
        case Qt::BottomEdge:
            if (view->geometry().bottom() > screenView->geometry().bottom()) {
                if (!candidate || view->geometry().bottom() < candidate->geometry().bottom()
                    || (view->geometry().bottom() == candidate->geometry().bottom()
                        && view->geometry().left() > candidate->geometry().left())) {
                    candidate = view.get();
                }
            }
            break;
        }
    }

    return candidate;
}

void QuickSceneEffect::activateView(QuickSceneView* view)
{
    if (!view) {
        return;
    }

    auto av = activeView();
    // Already properly active?
    if (view == av && av->window()->activeFocusItem()) {
        return;
    }

    for (auto const& [screen, otherView] : d->views) {
        if (otherView.get() == view && !view->window()->activeFocusItem()) {
            QFocusEvent focusEvent(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
            qApp->sendEvent(view->window(), &focusEvent);
        } else if (otherView.get() != view && otherView->window()->activeFocusItem()) {
            QFocusEvent focusEvent(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
            qApp->sendEvent(otherView->window(), &focusEvent);
        }
    }

    Q_EMIT activeViewChanged(view);
}

void QuickSceneEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);
    d->paintedScreen = data.screen;

    if (effects->waylandDisplay()) {
        if (auto it = d->views.find(data.screen); it != d->views.end()) {
            effects->renderOffscreenQuickView(it->second.get());
        }
    } else {
        for (auto const& [screen, screenView] : d->views) {
            effects->renderOffscreenQuickView(screenView.get());
        }
    }
}

void QuickSceneEffect::postPaintScreen()
{
    // Screen views are repainted after kwin performs its compositing cycle. Another alternative
    // is to update the views after receiving a vblank.
    if (effects->waylandDisplay()) {
        auto it = d->views.find(d->paintedScreen);
        if (it != d->views.end()) {
            if (auto view = it->second.get(); view->isDirty()) {
                QMetaObject::invokeMethod(view, &QuickSceneView::update, Qt::QueuedConnection);
                view->resetDirty();
            }
        }
    } else {
        for (auto const& [screen, screenView] : d->views) {
            if (screenView->isDirty()) {
                QMetaObject::invokeMethod(
                    screenView.get(), &QuickSceneView::update, Qt::QueuedConnection);
                screenView->resetDirty();
            }
        }
    }
    effects->postPaintScreen();
}

bool QuickSceneEffect::isActive() const
{
    return !d->views.empty() && !effects->isScreenLocked();
}

QVariantMap QuickSceneEffect::initialProperties(EffectScreen const* /*screen*/)
{
    return QVariantMap();
}

void QuickSceneEffect::handleScreenAdded(EffectScreen const* screen)
{
    addScreen(screen);
}

void QuickSceneEffect::handleScreenRemoved(EffectScreen const* screen)
{
    d->views.erase(screen);
    d->incubators.erase(screen);
    d->contexts.erase(screen);
}

void QuickSceneEffect::addScreen(EffectScreen const* screen)
{
    auto properties = initialProperties(screen);
    properties["width"] = screen->geometry().width();
    properties["height"] = screen->geometry().height();

    auto incubator = new QuickSceneViewIncubator(
        this, screen, [this, screen](QuickSceneViewIncubator* incubator) {
            if (incubator->isReady()) {
                auto view = incubator->result();
                if (view->contentItem()) {
                    view->contentItem()->setFocus(false);
                }
                connect(view.get(), &QuickSceneView::repaintNeeded, this, [screen]() {
                    effects->addRepaint(screen->geometry());
                });
                connect(view.get(),
                        &QuickSceneView::renderRequested,
                        view.get(),
                        &QuickSceneView::scheduleRepaint);
                connect(view.get(),
                        &QuickSceneView::sceneChanged,
                        view.get(),
                        &QuickSceneView::scheduleRepaint);
                view->scheduleRepaint();
                d->views[screen] = std::move(view);
            } else if (incubator->isError()) {
                qCWarning(KWIN_CORE)
                    << "Could not create a view for QML file" << d->delegate->url();
                qCWarning(KWIN_CORE) << incubator->errors();
            }
        });
    incubator->setInitialProperties(properties);

    QQmlContext* parentContext;
    if (QQmlContext* context = d->delegate->creationContext()) {
        parentContext = context;
    } else if (QQmlContext* context = qmlContext(this)) {
        parentContext = context;
    } else {
        parentContext = d->delegate->engine()->rootContext();
    }
    QQmlContext* context = new QQmlContext(parentContext);

    d->contexts[screen].reset(context);
    d->incubators[screen].reset(incubator);
    d->delegate->create(*incubator, context);
}

void QuickSceneEffect::startInternal()
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!d->delegate) {
        if (Q_UNLIKELY(d->source.isEmpty())) {
            qWarning() << "QuickSceneEffect.source is empty. Did you forget to call setSource()?";
            return;
        }

        d->delegate = std::make_unique<QQmlComponent>(effects->qmlEngine());
        d->delegate->loadUrl(d->source);
        if (d->delegate->isError()) {
            qWarning().nospace() << "Failed to load " << d->source << ": " << d->delegate->errors();
            d->delegate.reset();
            return;
        }
        Q_EMIT delegateChanged();
    }

    effects->setActiveFullScreenEffect(this);
    d->running = true;

    // Install an event filter to monitor cursor shape changes.
    qApp->installEventFilter(this);

    auto const screens = effects->screens();
    for (auto screen : screens) {
        addScreen(screen);
    }

    // Ensure one view has an active focus item
    activateView(activeView());

    connect(effects, &EffectsHandler::screenAdded, this, &QuickSceneEffect::handleScreenAdded);
    connect(effects, &EffectsHandler::screenRemoved, this, &QuickSceneEffect::handleScreenRemoved);

    effects->grabKeyboard(this);
    effects->startMouseInterception(this, Qt::ArrowCursor);
}

void QuickSceneEffect::stopInternal()
{
    disconnect(effects, &EffectsHandler::screenAdded, this, &QuickSceneEffect::handleScreenAdded);
    disconnect(
        effects, &EffectsHandler::screenRemoved, this, &QuickSceneEffect::handleScreenRemoved);

    d->incubators.clear();
    d->views.clear();
    d->contexts.clear();
    d->running = false;
    qApp->removeEventFilter(this);
    effects->ungrabKeyboard();
    effects->stopMouseInterception(this);
    effects->setActiveFullScreenEffect(nullptr);
    effects->addRepaintFull();
}

void QuickSceneEffect::windowInputMouseEvent(QEvent* event)
{
    Qt::MouseButtons buttons;
    QPoint globalPosition;
    if (QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event)) {
        buttons = mouseEvent->buttons();
        globalPosition = mouseEvent->globalPos();
    } else if (QWheelEvent* wheelEvent = dynamic_cast<QWheelEvent*>(event)) {
        buttons = wheelEvent->buttons();
        globalPosition = wheelEvent->globalPosition().toPoint();
    } else {
        return;
    }

    if (buttons) {
        if (!d->mouseImplicitGrab) {
            d->mouseImplicitGrab = viewAt(globalPosition);
        }
    }

    QuickSceneView* target = d->mouseImplicitGrab;
    if (!target) {
        target = viewAt(globalPosition);
    }

    if (!buttons) {
        d->mouseImplicitGrab = nullptr;
    }

    if (target) {
        if (buttons) {
            activateView(target);
        }
        target->forwardMouseEvent(event);
    }
}

void QuickSceneEffect::grabbedKeyboardEvent(QKeyEvent* keyEvent)
{
    auto screenView = activeView();

    if (screenView) {
        // ActiveView may not have an activeFocusItem yet
        activateView(screenView);
        screenView->forwardKeyEvent(keyEvent);
    }
}

bool QuickSceneEffect::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    for (auto const& [screen, screenView] : d->views) {
        if (screenView->geometry().contains(pos.toPoint())) {
            activateView(screenView.get());
            return screenView->forwardTouchDown(id, pos, time);
        }
    }
    return false;
}

bool QuickSceneEffect::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    for (auto const& [screen, screenView] : d->views) {
        if (screenView->geometry().contains(pos.toPoint())) {
            return screenView->forwardTouchMotion(id, pos, time);
        }
    }
    return false;
}

bool QuickSceneEffect::touchUp(qint32 id, quint32 time)
{
    for (auto const& [screen, screenView] : d->views) {
        if (screenView->forwardTouchUp(id, time)) {
            return true;
        }
    }
    return false;
}

} // namespace KWin

#include <moc_quick_scene.cpp>
