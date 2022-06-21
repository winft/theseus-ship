/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2007 Rivo Laks <rivolaks@hot.ee>
Copyright (C) 2008 Lucas Murray <lmurray@undefinedfire.com>

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
#include "invert.h"

#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KGlobalAccel>
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
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << Qt::CTRL + Qt::META + Qt::Key_I);
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << Qt::CTRL + Qt::META + Qt::Key_I);
    effects->registerGlobalShortcut(Qt::CTRL + Qt::META + Qt::Key_I, a);
    connect(a, &QAction::triggered, this, &InvertEffect::toggleScreenInversion);

    QAction* b = new QAction(this);
    b->setObjectName(QStringLiteral("InvertWindow"));
    b->setText(i18n("Toggle Invert Effect on Window"));
    KGlobalAccel::self()->setDefaultShortcut(
        b, QList<QKeySequence>() << Qt::CTRL + Qt::META + Qt::Key_U);
    KGlobalAccel::self()->setShortcut(b, QList<QKeySequence>() << Qt::CTRL + Qt::META + Qt::Key_U);
    effects->registerGlobalShortcut(Qt::CTRL + Qt::META + Qt::Key_U, b);
    connect(b, &QAction::triggered, this, &InvertEffect::toggleWindow);

    connect(effects, &EffectsHandler::windowClosed, this, &InvertEffect::slotWindowClosed);
}

InvertEffect::~InvertEffect() = default;

bool InvertEffect::supported()
{
    return effects->compositingType() == OpenGLCompositing;
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

void InvertEffect::drawWindow(EffectWindow* w,
                              int mask,
                              const QRegion& region,
                              WindowPaintData& data)
{
    // Load if we haven't already
    if (m_valid && !m_inited)
        m_valid = loadData();

    bool useShader = m_valid && (m_allWindows != m_windows.contains(w));
    if (useShader) {
        ShaderManager* shaderManager = ShaderManager::instance();
        shaderManager->pushShader(m_shader.get());

        data.shader = m_shader.get();
    }

    effects->drawWindow(w, mask, region, data);

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
