/*
SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_FALLAPART_H
#define KWIN_FALLAPART_H

#include <render/effect/interface/effect_window_deleted_ref.h>
#include <render/effect/interface/effect_window_visible_ref.h>
#include <render/effect/interface/offscreen_effect.h>

namespace KWin
{

struct FallApartAnimation {
    EffectWindowDeletedRef deletedRef;
    EffectWindowVisibleRef visibleRef;
    std::chrono::milliseconds lastPresentTime = std::chrono::milliseconds::zero();
    qreal progress = 0;
};

class FallApartEffect : public OffscreenEffect
{
    Q_OBJECT
    Q_PROPERTY(int blockSize READ configuredBlockSize)
public:
    FallApartEffect();
    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 70;
    }

    // for properties
    int configuredBlockSize() const
    {
        return blockSize;
    }

    static bool supported();

protected:
    void apply(effect::window_paint_data& data, WindowQuadList& quads) override;

public Q_SLOTS:
    void slotWindowClosed(KWin::EffectWindow* c);
    void slotWindowDeleted(KWin::EffectWindow* w);
    void slotWindowDataChanged(KWin::EffectWindow* w, int role);

private:
    QHash<EffectWindow*, FallApartAnimation> windows;
    bool isRealWindow(EffectWindow* w);
    int blockSize;
};

} // namespace

#endif
