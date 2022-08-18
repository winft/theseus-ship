/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "kwin_export.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QUuid>
#include <QWeakPointer>

namespace KWin
{

class EffectWindow;

namespace scripting
{
class window;
}

namespace render
{

class basic_thumbnail_item : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(qreal brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QQuickItem* clipTo READ clipTo WRITE setClipTo NOTIFY clipToChanged)
public:
    ~basic_thumbnail_item() override;
    qreal brightness() const;
    qreal saturation() const;
    QQuickItem* clipTo() const;

public Q_SLOTS:
    void setBrightness(qreal brightness);
    void setSaturation(qreal saturation);
    void setClipTo(QQuickItem* clip);

Q_SIGNALS:
    void brightnessChanged();
    void saturationChanged();
    void clipToChanged();

protected:
    explicit basic_thumbnail_item(QQuickItem* parent = nullptr);

protected Q_SLOTS:
    virtual void repaint(KWin::EffectWindow* w) = 0;

private Q_SLOTS:
    void effectWindowAdded();
    void compositingToggled();

private:
    void ensure_parent_effect_window();

    EffectWindow* m_parent{nullptr};
    qreal m_brightness;
    qreal m_saturation;
    QPointer<QQuickItem> m_clipToItem;
};

class KWIN_EXPORT window_thumbnail_item : public basic_thumbnail_item
{
    Q_OBJECT
    Q_PROPERTY(QUuid wId READ wId WRITE setWId NOTIFY wIdChanged SCRIPTABLE true)
    Q_PROPERTY(KWin::scripting::window* client READ client WRITE setClient NOTIFY clientChanged)
public:
    explicit window_thumbnail_item(QQuickItem* parent = nullptr);
    ~window_thumbnail_item() override;

    QUuid wId() const
    {
        return m_wId;
    }
    void setWId(const QUuid& wId);
    scripting::window* client() const;
    void setClient(scripting::window* window);
    void paint(QPainter* painter) override;
Q_SIGNALS:
    void wIdChanged(const QUuid& wid);
    void clientChanged();
protected Q_SLOTS:
    void repaint(KWin::EffectWindow* w) override;

private:
    QUuid m_wId;
    scripting::window* m_client;
};

class KWIN_EXPORT desktop_thumbnail_item : public basic_thumbnail_item
{
    Q_OBJECT
    Q_PROPERTY(int desktop READ desktop WRITE setDesktop NOTIFY desktopChanged)
public:
    desktop_thumbnail_item(QQuickItem* parent = nullptr);
    ~desktop_thumbnail_item() override;

    int desktop() const
    {
        return m_desktop;
    }
    void setDesktop(int desktop);
    void paint(QPainter* painter) override;
Q_SIGNALS:
    void desktopChanged(int desktop);
protected Q_SLOTS:
    void repaint(KWin::EffectWindow* w) override;

private:
    int m_desktop;
};

inline qreal basic_thumbnail_item::brightness() const
{
    return m_brightness;
}

inline qreal basic_thumbnail_item::saturation() const
{
    return m_saturation;
}

inline QQuickItem* basic_thumbnail_item::clipTo() const
{
    return m_clipToItem.data();
}

inline scripting::window* window_thumbnail_item::client() const
{
    return m_client;
}

}
}
