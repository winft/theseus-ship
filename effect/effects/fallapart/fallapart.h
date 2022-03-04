/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2007 Lubos Lunak <l.lunak@kde.org>

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

#ifndef KWIN_FALLAPART_H
#define KWIN_FALLAPART_H

#include <kwineffects/deform_effect.h>

namespace KWin
{

struct FallApartAnimation {
    std::chrono::milliseconds lastPresentTime = std::chrono::milliseconds::zero();
    qreal progress = 0;
};

class FallApartEffect : public DeformEffect
{
    Q_OBJECT
    Q_PROPERTY(int blockSize READ configuredBlockSize)
public:
    FallApartEffect();
    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
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
    void deform(EffectWindow* w, int mask, WindowPaintData& data, WindowQuadList& quads) override;

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
