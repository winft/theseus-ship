/*
SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2023 Andrew Shark <ashark at linuxcomp.ru>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MOUSEMARK_H
#define KWIN_MOUSEMARK_H

#include <render/effect/interface/effect.h>
#include <render/gl/interface/utils.h>

namespace KWin
{

class MouseMarkEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int width READ configuredWidth)
    Q_PROPERTY(QColor color READ configuredColor)
    Q_PROPERTY(Qt::KeyboardModifiers modifiers READ freedraw_modifiers)
    Q_PROPERTY(Qt::KeyboardModifiers modifiers READ arrowdraw_modifiers)
public:
    MouseMarkEffect();
    ~MouseMarkEffect() override;
    void reconfigure(ReconfigureFlags) override;
    void paintScreen(effect::screen_paint_data& data) override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override;

    // for properties
    int configuredWidth() const
    {
        return width;
    }
    QColor configuredColor() const
    {
        return color;
    }
    Qt::KeyboardModifiers freedraw_modifiers() const
    {
        return m_freedraw_modifiers;
    }
    Qt::KeyboardModifiers arrowdraw_modifiers() const
    {
        return m_freedraw_modifiers;
    }
private Q_SLOTS:
    void clear();
    void clearLast();
    void slotMouseChanged(const QPoint& pos,
                          const QPoint& old,
                          Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons,
                          Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);
    void screenLockingChanged(bool locked);

private:
    typedef QVector<QPoint> Mark;
    void drawMark(QPainter* painter, const Mark& mark);
    static Mark createArrow(QPoint arrow_head, QPoint arrow_tail);
    QVector<Mark> marks;
    Mark drawing;
    QPoint arrow_tail;
    int width;
    QColor color;
    Qt::KeyboardModifiers m_freedraw_modifiers;
    Qt::KeyboardModifiers m_arrowdraw_modifiers;
};

} // namespace

#endif
