/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_MOCK_ABSTRACT_CLIENT_H
#define KWIN_MOCK_ABSTRACT_CLIENT_H

#include <QObject>
#include <QRect>

#include <memory>

namespace KWin
{
class AbstractClient;

class mock_control
{
    bool m_active{false};

    AbstractClient* m_win;

public:
    explicit mock_control(AbstractClient* win);

    bool active() const;
};

class AbstractClient : public QObject
{
    Q_OBJECT
public:
    explicit AbstractClient(QObject *parent);
    ~AbstractClient() override;

    int screen() const;
    bool isOnScreen(int screen) const;
    bool isFullScreen() const;
    bool isHiddenInternal() const;
    QRect frameGeometry() const;

    void setScreen(int screen);
    void setFullScreen(bool set);
    void setHiddenInternal(bool set);
    void setFrameGeometry(const QRect &rect);
    bool isResize() const;
    void setResize(bool set);
    virtual void showOnScreenEdge() = 0;

    mock_control* control() const { return m_control.get(); }

Q_SIGNALS:
    void geometryChanged();
    void keepBelowChanged();

private:
    std::unique_ptr<mock_control> m_control;
    int m_screen;
    bool m_fullscreen;
    bool m_hiddenInternal;
    QRect m_frameGeometry;
    bool m_resize;
};

}

#endif
