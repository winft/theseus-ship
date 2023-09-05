/*
SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "invert.h"

#include <render/effect/interface/effect_frame.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/platform.h>
#include <render/gl/interface/utils.h>

#include <KLocalizedString>
#include <QAction>
#include <QFile>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(KWIN_INVERT, "kwin_effect_invert", QtWarningMsg)

static void ensureResources()
{
    // Must initialize resources manually because the effect is a static lib.
    Q_INIT_RESOURCE(invert);
}

namespace KWin
{

InvertEffect::InvertEffect()
    : m_inited(false)
    , m_valid(true)
    , m_shader(nullptr)
    , m_allWindows(false)
{
    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("Invert"));
    a->setText(i18n("Toggle Invert Effect"));
    effects->registerGlobalShortcutAndDefault({Qt::CTRL | Qt::META | Qt::Key_I}, a);
    connect(a, &QAction::triggered, this, &InvertEffect::toggleScreenInversion);

    QAction* b = new QAction(this);
    b->setObjectName(QStringLiteral("InvertWindow"));
    b->setText(i18n("Toggle Invert Effect on Window"));
    effects->registerGlobalShortcutAndDefault({Qt::CTRL | Qt::META | Qt::Key_U}, b);
    connect(b, &QAction::triggered, this, &InvertEffect::toggleWindow);

    connect(effects, &EffectsHandler::windowClosed, this, &InvertEffect::slotWindowClosed);
}

InvertEffect::~InvertEffect() = default;

bool InvertEffect::supported()
{
    return effects->isOpenGLCompositing();
}

bool InvertEffect::loadData()
{
    ensureResources();
    m_inited = true;

    m_shader = std::unique_ptr<GLShader>(ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture,
        QString(),
        QStringLiteral(":/effects/invert/shaders/invert.frag")));
    if (!m_shader->isValid()) {
        qCCritical(KWIN_INVERT) << "The shader failed to load!";
        return false;
    }

    return true;
}

void InvertEffect::drawWindow(effect::window_paint_data& data)
{
    // Load if we haven't already
    if (m_valid && !m_inited)
        m_valid = loadData();

    auto useShader = m_valid && (m_allWindows != m_windows.contains(&data.window));
    if (useShader) {
        ShaderManager* shaderManager = ShaderManager::instance();
        shaderManager->pushShader(m_shader.get());

        data.shader = m_shader.get();
    }

    effects->drawWindow(data);

    if (useShader) {
        ShaderManager::instance()->popShader();
    }
}

void InvertEffect::slotWindowClosed(EffectWindow* w)
{
    m_windows.removeOne(w);
}

void InvertEffect::toggleScreenInversion()
{
    m_allWindows = !m_allWindows;
    effects->addRepaintFull();
}

void InvertEffect::toggleWindow()
{
    if (!effects->activeWindow()) {
        return;
    }
    if (!m_windows.contains(effects->activeWindow()))
        m_windows.append(effects->activeWindow());
    else
        m_windows.removeOne(effects->activeWindow());
    effects->activeWindow()->addRepaintFull();
}

bool InvertEffect::isActive() const
{
    return m_valid && (m_allWindows || !m_windows.isEmpty());
}

bool InvertEffect::provides(Feature f)
{
    return f == ScreenInversion;
}

} // namespace
