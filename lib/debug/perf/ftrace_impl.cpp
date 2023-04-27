/*
SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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

FtraceImpl& FtraceImpl::instance()
{
    static FtraceImpl impl;
    return impl;
}

bool FtraceImpl::setEnabled(bool enable)
{
    if (static_cast<bool>(m_file) == enable) {
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
        m_file.reset();
    }
    return true;
}

void FtraceImpl::print(const QString& message)
{
    (*s_writeFunction)(m_file.get(), message);
}

void FtraceImpl::printBegin(const QString& message, ulong ctx)
{
    (*s_writeFunction)(m_file.get(), message + QStringLiteral(" (begin_ctx=%1)").arg(ctx));
}

void FtraceImpl::printEnd(const QString& message, ulong ctx)
{
    (*s_writeFunction)(m_file.get(), message + QStringLiteral(" (end_ctx=%1)").arg(ctx));
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
        if (dirPath.isEmpty() || !QFileInfo::exists(dirPath)) {
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
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_CORE) << "No access to trace marker file at:" << path;
        m_file.reset();
        return false;
    }
    return true;
}

}
}
