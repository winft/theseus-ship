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
#include "ftrace_impl.h"

#include "base/logging.h"

#include <QDir>
#include <QFileInfo>

namespace KWin
{
namespace Perf
{

void writeFunctionEnabled(QFile* file, const QString& message)
{
    file->write(message.toLatin1());
    file->flush();
}

void writeFunctionDisabled(QFile* file, const QString& message)
{
    Q_UNUSED(file)
    Q_UNUSED(message)
}

void (*s_writeFunction)(QFile* file, const QString& message) = writeFunctionDisabled;

KWIN_SINGLETON_FACTORY(FtraceImpl)

FtraceImpl::FtraceImpl(QObject* parent)
    : QObject(parent)
{
    if (qEnvironmentVariableIsSet("KWIN_PERF_FTRACE")) {
        qCDebug(KWIN_CORE) << "Ftrace marking initially enabled via environment variable";
        setEnabled(true);
    }
}

bool FtraceImpl::setEnabled(bool enable)
{
    if ((bool)m_file == enable) {
        // no change
        return true;
    }
    if (enable) {
        if (!findFile()) {
            qCWarning(KWIN_CORE)
                << "Ftrace marking not available. Try reenabling after issue is solved.";
            return false;
        }
        s_writeFunction = writeFunctionEnabled;
    } else {
        s_writeFunction = writeFunctionDisabled;
        delete m_file;
        m_file = nullptr;
    }
    return true;
}

void FtraceImpl::print(const QString& message)
{
    (*s_writeFunction)(m_file, message);
}

void FtraceImpl::printBegin(const QString& message, ulong ctx)
{
    (*s_writeFunction)(m_file, message + QStringLiteral(" (begin_ctx=%1)").arg(ctx));
}

void FtraceImpl::printEnd(const QString& message, ulong ctx)
{
    (*s_writeFunction)(m_file, message + QStringLiteral(" (end_ctx=%1)").arg(ctx));
}

bool FtraceImpl::findFile()
{
    QFile mountsFile("/proc/mounts");
    if (!mountsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(KWIN_CORE)
            << "No access to mounts file. Can not determine trace marker file location.";
        return false;
    }

    auto lineInfo = [](const QString& line) {
        const int start = line.indexOf(' ') + 1;
        const int end = line.indexOf(' ', start);
        const QString dirPath(line.mid(start, end - start));
        if (dirPath.isEmpty() || !QFileInfo(dirPath).exists()) {
            return QFileInfo();
        }
        return QFileInfo(QDir(dirPath), QStringLiteral("trace_marker"));
    };
    QFileInfo markerFileInfo;
    QTextStream mountsIn(&mountsFile);
    QString mountsLine = mountsIn.readLine();

    const QString name1("tracefs");
    const QString name2("debugfs");
    auto nameCompare = [](const QString& line, const QString& name) {
        const auto lineSplit = line.split(' ');
        if (lineSplit.size() <= 2) {
            return false;
        }
        return line.startsWith(name) || lineSplit.at(2) == name;
    };

    while (!mountsLine.isNull()) {
        if (nameCompare(mountsLine, name1)) {
            const auto info = lineInfo(mountsLine);
            if (info.exists()) {
                markerFileInfo = info;
                break;
            }
        }
        if (nameCompare(mountsLine, name2)) {
            markerFileInfo = lineInfo(mountsLine);
        }
        mountsLine = mountsIn.readLine();
    }
    mountsFile.close();
    if (!markerFileInfo.exists()) {
        qCWarning(KWIN_CORE) << "Could not determine trace marker file location from mounts.";
        return false;
    }

    const QString path = markerFileInfo.absoluteFilePath();
    m_file = new QFile(path, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_CORE) << "No access to trace marker file at:" << path;
        delete m_file;
        m_file = nullptr;
        return false;
    }
    return true;
}

}
}
