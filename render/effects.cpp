/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#include "effects.h"

#include "compositor.h"
#include "effect/frame.h"
#include "effectsadaptor.h"
#include "gl/backend.h"
#include "gl/scene.h"
#include "platform.h"
#include "singleton_interface.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "input/cursor.h"
#include "input/pointer_redirect.h"
#include "scripting/effect.h"
#include "win/control.h"
#include "win/deco/bridge.h"
#include "win/desktop_get.h"
#include "win/internal_window.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/window_area.h"
#include "win/x11/window.h"

#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KDecoration2/DecorationSettings>

namespace KWin::render
{

effects_handler_wrap::effects_handler_wrap(CompositingType type)
    : EffectsHandler(type)
    , m_effectLoader(new effect_loader(*this, this))
{
    qRegisterMetaType<QVector<KWin::EffectWindow*>>();

    singleton_interface::effects = this;
    connect(m_effectLoader,
            &basic_effect_loader::effectLoaded,
            this,
            [this](Effect* effect, const QString& name) {
                effect_order.insert(effect->requestedEffectChainPosition(),
                                    EffectPair(name, effect));
                loaded_effects << EffectPair(name, effect);
                effectsChanged();
            });
    m_effectLoader->setConfig(kwinApp()->config());

    new EffectsAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Effects"), this);

    // init is important, otherwise causes crashes when quads are build before the first painting
    // pass start
    m_currentBuildQuadsIterator = m_activeEffects.constEnd();
}

effects_handler_wrap::~effects_handler_wrap()
{
    singleton_interface::effects = nullptr;
}

void effects_handler_wrap::unloadAllEffects()
{
    for (const EffectPair& pair : qAsConst(loaded_effects)) {
        destroyEffect(pair.second);
    }

    effect_order.clear();
    m_effectLoader->clear();

    effectsChanged();
}

void effects_handler_wrap::reconfigure()
{
    m_effectLoader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void effects_handler_wrap::prePaintScreen(ScreenPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data, presentTime);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintScreen(int mask, const QRegion& region, ScreenPaintData& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(mask, region, data);
        --m_currentPaintScreenIterator;
    } else {
        final_paint_screen(static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_wrap::paintDesktop(int desktop,
                                        int mask,
                                        QRegion region,
                                        ScreenPaintData& data)
{
    if (desktop < 1 || desktop > numberOfDesktops()) {
        return;
    }
    m_currentRenderedDesktop = desktop;
    m_desktopRendering = true;
    // save the paint screen iterator
    EffectsIterator savedIterator = m_currentPaintScreenIterator;
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
    paintScreen(mask, region, data);
    // restore the saved iterator
    m_currentPaintScreenIterator = savedIterator;
    m_desktopRendering = false;
}

void effects_handler_wrap::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::prePaintWindow(EffectWindow* w,
                                          WindowPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(w, data, presentTime);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintWindow(EffectWindow* w,
                                       int mask,
                                       const QRegion& region,
                                       WindowPaintData& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(w, mask, region, data);
        --m_currentPaintWindowIterator;
    } else {
        final_paint_window(w, static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_wrap::postPaintWindow(EffectWindow* w)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->postPaintWindow(w);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

Effect* effects_handler_wrap::provides(Effect::Feature ef)
{
    for (int i = 0; i < loaded_effects.size(); ++i)
        if (loaded_effects.at(i).second->provides(ef))
            return loaded_effects.at(i).second;
    return nullptr;
}

void effects_handler_wrap::drawWindow(EffectWindow* w,
                                      int mask,
                                      const QRegion& region,
                                      WindowPaintData& data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(w, mask, region, data);
        --m_currentDrawWindowIterator;
    } else {
        final_draw_window(w, static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_wrap::buildQuads(EffectWindow* w, WindowQuadList& quadList)
{
    static bool initIterator = true;
    if (initIterator) {
        m_currentBuildQuadsIterator = m_activeEffects.constBegin();
        initIterator = false;
    }
    if (m_currentBuildQuadsIterator != m_activeEffects.constEnd()) {
        (*m_currentBuildQuadsIterator++)->buildQuads(w, quadList);
        --m_currentBuildQuadsIterator;
    }
    if (m_currentBuildQuadsIterator == m_activeEffects.constBegin())
        initIterator = true;
}

bool effects_handler_wrap::hasDecorationShadows() const
{
    return false;
}

bool effects_handler_wrap::decorationsHaveAlpha() const
{
    return true;
}

// start another painting pass
void effects_handler_wrap::startPaint()
{
    m_activeEffects.clear();
    m_activeEffects.reserve(loaded_effects.count());
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->isActive()) {
            m_activeEffects << it->second;
        }
    }
    m_currentDrawWindowIterator = m_activeEffects.constBegin();
    m_currentPaintWindowIterator = m_activeEffects.constBegin();
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
}

void effects_handler_wrap::slotCurrentTabAboutToChange(EffectWindow* from, EffectWindow* to)
{
    Q_EMIT currentTabAboutToChange(from, to);
}

void effects_handler_wrap::slotTabAdded(EffectWindow* w, EffectWindow* to)
{
    Q_EMIT tabAdded(w, to);
}

void effects_handler_wrap::slotTabRemoved(EffectWindow* w, EffectWindow* leaderOfFormerGroup)
{
    Q_EMIT tabRemoved(w, leaderOfFormerGroup);
}

void effects_handler_wrap::setActiveFullScreenEffect(Effect* e)
{
    if (fullscreen_effect == e) {
        return;
    }
    const bool activeChanged = (e == nullptr || fullscreen_effect == nullptr);
    fullscreen_effect = e;
    Q_EMIT activeFullScreenEffectChanged();
    if (activeChanged) {
        Q_EMIT hasActiveFullScreenEffectChanged();
    }
}

Effect* effects_handler_wrap::activeFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_wrap::hasActiveFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_wrap::grabKeyboard(Effect* effect)
{
    if (keyboard_grab_effect != nullptr)
        return false;
    if (!doGrabKeyboard()) {
        return false;
    }
    keyboard_grab_effect = effect;
    return true;
}

bool effects_handler_wrap::doGrabKeyboard()
{
    return true;
}

void effects_handler_wrap::ungrabKeyboard()
{
    Q_ASSERT(keyboard_grab_effect != nullptr);
    doUngrabKeyboard();
    keyboard_grab_effect = nullptr;
}

void effects_handler_wrap::doUngrabKeyboard()
{
}

void effects_handler_wrap::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (keyboard_grab_effect != nullptr)
        keyboard_grab_effect->grabbedKeyboardEvent(e);
}

void effects_handler_wrap::startMouseInterception(Effect* effect, Qt::CursorShape shape)
{
    if (m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.append(effect);
    if (m_grabbedMouseEffects.size() != 1) {
        return;
    }
    doStartMouseInterception(shape);
}

void effects_handler_wrap::stopMouseInterception(Effect* effect)
{
    if (!m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.removeAll(effect);
    if (m_grabbedMouseEffects.isEmpty()) {
        doStopMouseInterception();
    }
}

bool effects_handler_wrap::isMouseInterception() const
{
    return m_grabbedMouseEffects.count() > 0;
}

bool effects_handler_wrap::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchDown(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_wrap::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchMotion(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_wrap::touchUp(qint32 id, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchUp(id, time)) {
            return true;
        }
    }
    return false;
}

void* effects_handler_wrap::getProxy(QString name)
{
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name)
            return (*it).second->proxy();

    return nullptr;
}

bool effects_handler_wrap::hasKeyboardGrab() const
{
    return keyboard_grab_effect != nullptr;
}

QString effects_handler_wrap::currentActivity() const
{
    return QString();
}

int effects_handler_wrap::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int effects_handler_wrap::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int effects_handler_wrap::workspaceWidth() const
{
    return desktopGridWidth() * kwinApp()->get_base().topology.size.width();
}

int effects_handler_wrap::workspaceHeight() const
{
    return desktopGridHeight() * kwinApp()->get_base().topology.size.height();
}

bool effects_handler_wrap::optionRollOverDesktops() const
{
    return kwinApp()->options->qobject->isRollOverDesktops();
}

double effects_handler_wrap::animationTimeFactor() const
{
    return kwinApp()->options->animationTimeFactor();
}

WindowQuadType effects_handler_wrap::newWindowQuadType()
{
    return WindowQuadType(next_window_quad_type++);
}

void effects_handler_wrap::setElevatedWindow(KWin::EffectWindow* w, bool set)
{
    elevated_windows.removeAll(w);
    if (set)
        elevated_windows.append(w);
}

bool effects_handler_wrap::checkInputWindowEvent(QMouseEvent* e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (auto const& effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

bool effects_handler_wrap::checkInputWindowEvent(QWheelEvent* e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (auto const& effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

void effects_handler_wrap::checkInputWindowStacking()
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return;
    }
    doCheckInputWindowStacking();
}

void effects_handler_wrap::doCheckInputWindowStacking()
{
}

void effects_handler_wrap::toggleEffect(const QString& name)
{
    if (isEffectLoaded(name))
        unloadEffect(name);
    else
        loadEffect(name);
}

QStringList effects_handler_wrap::loadedEffects() const
{
    QStringList listModules;
    listModules.reserve(loaded_effects.count());
    std::transform(loaded_effects.constBegin(),
                   loaded_effects.constEnd(),
                   std::back_inserter(listModules),
                   [](const EffectPair& pair) { return pair.first; });
    return listModules;
}

QStringList effects_handler_wrap::listOfEffects() const
{
    return m_effectLoader->listOfKnownEffects();
}

bool effects_handler_wrap::loadEffect(const QString& name)
{
    makeOpenGLContextCurrent();
    addRepaintFull();

    return m_effectLoader->loadEffect(name);
}

void effects_handler_wrap::unloadEffect(const QString& name)
{
    auto it = std::find_if(effect_order.begin(), effect_order.end(), [name](EffectPair& pair) {
        return pair.first == name;
    });
    if (it == effect_order.end()) {
        qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Effect not loaded :" << name;
        return;
    }

    qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Unloading Effect :" << name;
    destroyEffect((*it).second);
    effect_order.erase(it);
    effectsChanged();

    addRepaintFull();
}

void effects_handler_wrap::destroyEffect(Effect* effect)
{
    assert(effect);
    makeOpenGLContextCurrent();

    if (fullscreen_effect == effect) {
        setActiveFullScreenEffect(nullptr);
    }

    if (keyboard_grab_effect == effect) {
        ungrabKeyboard();
    }

    stopMouseInterception(effect);
    handle_effect_destroy(*effect);

    const QList<QByteArray> properties = m_propertiesForEffects.keys();
    for (const QByteArray& property : properties) {
        removeSupportProperty(property, effect);
    }

    delete effect;
}

void effects_handler_wrap::reconfigureEffect(const QString& name)
{
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name) {
            kwinApp()->config()->reparseConfiguration();
            makeOpenGLContextCurrent();
            (*it).second->reconfigure(Effect::ReconfigureAll);
            return;
        }
}

bool effects_handler_wrap::isEffectLoaded(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [&name](const EffectPair& pair) { return pair.first == name; });
    return it != loaded_effects.constEnd();
}

bool effects_handler_wrap::isEffectSupported(const QString& name)
{
    // If the effect is loaded, it is obviously supported.
    if (isEffectLoaded(name)) {
        return true;
    }

    // next checks might require a context
    makeOpenGLContextCurrent();

    return m_effectLoader->isEffectSupported(name);
}

QList<bool> effects_handler_wrap::areEffectsSupported(const QStringList& names)
{
    QList<bool> retList;
    retList.reserve(names.count());
    std::transform(names.constBegin(),
                   names.constEnd(),
                   std::back_inserter(retList),
                   [this](const QString& name) { return isEffectSupported(name); });
    return retList;
}

void effects_handler_wrap::reloadEffect(Effect* effect)
{
    QString effectName;
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).second == effect) {
            effectName = (*it).first;
            break;
        }
    }
    if (!effectName.isNull()) {
        unloadEffect(effectName);
        m_effectLoader->loadEffect(effectName);
    }
}

void effects_handler_wrap::effectsChanged()
{
    loaded_effects.clear();
    m_activeEffects.clear(); // it's possible to have a reconfigure and a quad rebuild between two
                             // paint cycles - bug #308201

    loaded_effects.reserve(effect_order.count());
    std::copy(
        effect_order.constBegin(), effect_order.constEnd(), std::back_inserter(loaded_effects));

    m_activeEffects.reserve(loaded_effects.count());
}

QList<EffectWindow*> effects_handler_wrap::elevatedWindows() const
{
    if (isScreenLocked()) {
        return {};
    }
    return elevated_windows;
}

QStringList effects_handler_wrap::activeEffects() const
{
    QStringList ret;
    for (auto it = loaded_effects.constBegin(), end = loaded_effects.constEnd(); it != end; ++it) {
        if (it->second->isActive()) {
            ret << it->first;
        }
    }
    return ret;
}

Wrapland::Server::Display* effects_handler_wrap::waylandDisplay() const
{
    return nullptr;
}

EffectFrame* effects_handler_wrap::effectFrame(EffectFrameStyle style,
                                               bool staticSize,
                                               const QPoint& position,
                                               Qt::Alignment alignment) const
{
    return new effect_frame_impl(
        const_cast<effects_handler_wrap&>(*this), style, staticSize, position, alignment);
}

QString effects_handler_wrap::supportInformation(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [name](const EffectPair& pair) { return pair.first == name; });
    if (it == loaded_effects.constEnd()) {
        return QString();
    }

    QString support((*it).first + QLatin1String(":\n"));
    const QMetaObject* metaOptions = (*it).second->metaObject();
    for (int i = 0; i < metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (qstrcmp(property.name(), "objectName") == 0) {
            continue;
        }
        support += QString::fromUtf8(property.name()) + QLatin1String(": ")
            + (*it).second->property(property.name()).toString() + QLatin1Char('\n');
    }

    return support;
}

bool effects_handler_wrap::isScreenLocked() const
{
    return kwinApp()->screen_locker_watcher->is_locked();
}

QString effects_handler_wrap::debug(const QString& name, const QString& parameter) const
{
    QString internalName = name.toLower();
    ;
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).first == internalName) {
            return it->second->debug(parameter);
        }
    }
    return QString();
}

xcb_connection_t* effects_handler_wrap::xcbConnection() const
{
    return connection();
}

xcb_window_t effects_handler_wrap::x11RootWindow() const
{
    return rootWindow();
}

void effects_handler_wrap::highlightWindows(const QVector<EffectWindow*>& windows)
{
    Effect* e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
}

KSharedConfigPtr effects_handler_wrap::config() const
{
    return kwinApp()->config();
}

KSharedConfigPtr effects_handler_wrap::inputConfig() const
{
    return kwinApp()->inputConfig();
}

Effect* effects_handler_wrap::findEffect(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [name](const EffectPair& pair) { return pair.first == name; });
    if (it == loaded_effects.constEnd()) {
        return nullptr;
    }
    return (*it).second;
}

QImage effects_handler_wrap::blit_from_framebuffer(QRect const& geometry, double scale) const
{
    if (!isOpenGLCompositing()) {
        return {};
    }

    QImage image;
    auto const nativeSize = geometry.size() * scale;

    if (GLRenderTarget::blitSupported() && !GLPlatform::instance()->isGLES()) {
        image = QImage(nativeSize.width(), nativeSize.height(), QImage::Format_ARGB32);

        GLTexture texture(GL_RGBA8, nativeSize.width(), nativeSize.height());
        GLRenderTarget target(texture);
        target.blitFromFramebuffer(mapToRenderTarget(geometry));

        // Copy content from framebuffer into image.
        texture.bind();
        glGetTexImage(
            GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLvoid*>(image.bits()));
        texture.unbind();
    } else {
        image = QImage(nativeSize.width(), nativeSize.height(), QImage::Format_RGBA8888);
        glReadPixels(0,
                     0,
                     nativeSize.width(),
                     nativeSize.height(),
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     static_cast<GLvoid*>(image.bits()));
    }

    image.setDevicePixelRatio(scale);

    if (waylandDisplay()) {
        return image.mirrored();
    }

    return image;
}

bool effects_handler_wrap::invert_screen()
{
    if (auto inverter = provides(Effect::ScreenInversion)) {
        qCDebug(KWIN_CORE) << "inverting screen using Effect plugin";
        QMetaObject::invokeMethod(inverter, "toggleScreenInversion", Qt::DirectConnection);
        return true;
    }
    return false;
}

}