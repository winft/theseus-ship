/*
SPDX-FileCopyrightText: 2007 Philip Falkner <philip.falkner@gmail.com>
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SHEET_H
#define KWIN_SHEET_H

#include <kwineffects/effect.h>
#include <kwineffects/effect_window_deleted_ref.h>
#include <kwineffects/effect_window_visible_ref.h>
#include <kwineffects/time_line.h>

namespace KWin
{

class SheetEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int duration READ duration)

public:
    SheetEffect();

    void reconfigure(ReconfigureFlags flags) override;

    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(effect::window_paint_data& data) override;
    void postPaintWindow(EffectWindow* w) override;

    bool isActive() const override;
    int requestedEffectChainPosition() const override;

    static bool supported();

    int duration() const;

private Q_SLOTS:
    void slotWindowAdded(EffectWindow* w);
    void slotWindowClosed(EffectWindow* w);
    void slotWindowDeleted(EffectWindow* w);

private:
    bool isSheetWindow(EffectWindow* w) const;

private:
    std::chrono::milliseconds m_duration;

    struct Animation {
        EffectWindowDeletedRef deletedRef;
        EffectWindowVisibleRef visibleRef;
        TimeLine timeLine;
        int parentY;
    };

    QHash<EffectWindow*, Animation> m_animations;
};

inline int SheetEffect::requestedEffectChainPosition() const
{
    return 60;
}

inline int SheetEffect::duration() const
{
    return m_duration.count();
}

} // namespace KWin

#endif
