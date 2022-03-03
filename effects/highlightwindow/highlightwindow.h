/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>

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

#ifndef KWIN_HIGHLIGHTWINDOW_H
#define KWIN_HIGHLIGHTWINDOW_H

#include <kwineffects/animation_effect.h>

namespace KWin
{

class HighlightWindowEffect : public AnimationEffect
{
    Q_OBJECT

public:
    HighlightWindowEffect();
    ~HighlightWindowEffect() override;

    int requestedEffectChainPosition() const override
    {
        return 70;
    }

    bool provides(Feature feature) override;
    bool perform(Feature feature, const QVariantList& arguments) override;
    Q_SCRIPTABLE void highlightWindows(const QStringList& windows);

public Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow* w);
    void slotWindowClosed(KWin::EffectWindow* w);
    void slotWindowDeleted(KWin::EffectWindow* w);

private:
    void startGhostAnimation(EffectWindow* window, int duration = -1);
    void startHighlightAnimation(EffectWindow* window, int duration = -1);
    void startRevertAnimation(EffectWindow* window);

    bool isHighlighted(EffectWindow* window) const;

    void prepareHighlighting();
    void finishHighlighting();
    void highlightWindows(const QVector<KWin::EffectWindow*>& windows);

    QList<EffectWindow*> m_highlightedWindows;
    QHash<EffectWindow*, quint64> m_animations;
    QEasingCurve m_easingCurve;
    int m_fadeDuration;
    EffectWindow* m_monitorWindow;
    QList<WId> m_highlightedIds;
    float m_ghostOpacity = 0;
};

} // namespace

#endif
