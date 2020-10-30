/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2020 Roman Gilg <subdiff@gmail.com>

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

#include <kwin_export.h>

#include <QHash>
#include <QObject>

#include <deque>
#include <time.h>

class QElapsedTimer;

namespace Wrapland
{
namespace Server
{
class PresentationFeedback;
class Surface;
}
}

namespace KWin
{

class AbstractWaylandOutput;
class Toplevel;

/**
 * TODO
 */
class KWIN_EXPORT Presentation : public QObject
{
    Q_OBJECT
public:
    enum class Kind {
        None            = 0,
        Vsync           = 1 << 0,
        HwClock         = 1 << 1,
        HwCompletion    = 1 << 2,
        ZeroCopy        = 1 << 3,
    };
    Q_DECLARE_FLAGS(Kinds, Kind)

    Presentation(QObject *parent = nullptr);
    ~Presentation() override;

    bool initClock(bool clockIdValid, clockid_t clockId);

    void lock(AbstractWaylandOutput* output, std::deque<Toplevel*> const& windows);
    void presented(AbstractWaylandOutput* output, uint32_t sec, uint32_t usec, Kinds kinds);
    void softwarePresented(Kinds kinds);

private:
    uint32_t currentTime() const;

    QHash<uint32_t, Wrapland::Server::Surface*> m_surfaces;

    clockid_t m_clockId;
    QElapsedTimer *m_fallbackClock = nullptr;
};

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Presentation::Kinds)
