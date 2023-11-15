/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "effect/frame.h"
#include "effectsadaptor.h"
#include "singleton_interface.h"

#include "base/logging.h"
#include "win/control.h"
#include "win/deco/bridge.h"
#include "win/desktop_get.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/window_area.h"
#include "win/x11/window.h"

#include <render/gl/interface/framebuffer.h>
#include <render/gl/interface/platform.h>

#include <KDecoration2/DecorationSettings>

namespace KWin::render
{

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
    loader->clear();

    effectsChanged();
}

void effects_handler_wrap::reconfigure()
{
    loader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void effects_handler_wrap::prePaintScreen(effect::screen_prepaint_data& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintScreen(effect::screen_paint_data& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(data);
        --m_currentPaintScreenIterator;
    } else {
        final_paint_screen(static_cast<render::paint_type>(data.paint.mask), data);
    }
}

void effects_handler_wrap::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::prePaintWindow(effect::window_prepaint_data& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(data);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintWindow(effect::window_paint_data& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(data);
        --m_currentPaintWindowIterator;
    } else {
        final_paint_window(data);
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

void effects_handler_wrap::drawWindow(effect::window_paint_data& data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(data);
        --m_currentDrawWindowIterator;
    } else {
        final_draw_window(data);
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

double effects_handler_wrap::animationTimeFactor() const
{
    return options.animationTimeFactor();
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
    return loader->listOfKnownEffects();
}

bool effects_handler_wrap::loadEffect(const QString& name)
{
    makeOpenGLContextCurrent();
    addRepaintFull();

    return loader->loadEffect(name);
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

void effects_handler_wrap::create_adaptor()
{
    new EffectsAdaptor(this);
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

    return loader->isEffectSupported(name);
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
        loader->loadEffect(effectName);
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

std::unique_ptr<EffectFrame> effects_handler_wrap::effectFrame(EffectFrameStyle style,
                                                               bool staticSize,
                                                               const QPoint& position,
                                                               Qt::Alignment alignment) const
{
    return std::make_unique<effect_frame_impl>(
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

void effects_handler_wrap::highlightWindows(const QVector<EffectWindow*>& windows)
{
    Effect* e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
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

QImage effects_handler_wrap::blit_from_framebuffer(effect::render_data& data,
                                                   QRect const& geometry,
                                                   double scale) const
{
    if (!isOpenGLCompositing()) {
        return {};
    }

    auto const screen_geometry = effect::map_to_viewport(data, geometry);
    auto const nativeSize = screen_geometry.size() * scale;
    QImage image(nativeSize, QImage::Format_ARGB32);

    if (GLFramebuffer::blitSupported() && !GLPlatform::instance()->isGLES()) {
        image = QImage(nativeSize.width(), nativeSize.height(), QImage::Format_ARGB32);

        GLTexture texture(GL_RGBA8, nativeSize.width(), nativeSize.height());
        GLFramebuffer target(&texture);
        target.blit_from_current_render_target(data, geometry, QRect({}, geometry.size()));

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
