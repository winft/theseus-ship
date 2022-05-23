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

#include <QFile>
#include <QString>
#include <memory>

namespace KWin
{
namespace Perf
{

/**
 * Provides an interface to mark the Ftrace output for debugging.
 */
class FtraceImpl
{
public:
    static FtraceImpl& instance();

    /**
     * @brief Enables or disables the marker
     *
     * @param enable The enablement state to set
     * @return True if setting enablement succeeded, else false
     */
    bool setEnabled(bool enable);
    void print(const QString& message);
    void printBegin(const QString& message, ulong ctx);
    void printEnd(const QString& message, ulong ctx);

private:
    FtraceImpl() = default;
    bool findFile();

    std::unique_ptr<QFile> m_file;
};

}
}
